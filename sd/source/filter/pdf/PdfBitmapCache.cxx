/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PdfBitmapCache.hxx"
#include "PdfSharedDocument.hxx"

#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <vcl/bitmap.hxx>
#include <tools/link.hxx>

namespace sd {

// ---------------------------------------------------------------------------
// Helper for posting repaint callbacks to the main thread
// ---------------------------------------------------------------------------
namespace {

struct RepaintEventData {
    std::vector<std::function<void()>> callbacks;
};

static void invokeRepaintCallbacks(void* /*pInstance*/, void* pArg)
{
    std::unique_ptr<RepaintEventData> pData(static_cast<RepaintEventData*>(pArg));
    for (auto& fn : pData->callbacks)
        fn();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RenderKey
// ---------------------------------------------------------------------------

bool RenderKey::operator<(const RenderKey& o) const
{
    if (pageIndex != o.pageIndex)
        return pageIndex < o.pageIndex;
    if (pixelWidth != o.pixelWidth)
        return pixelWidth < o.pixelWidth;
    return pixelHeight < o.pixelHeight;
}

bool RenderKey::operator==(const RenderKey& o) const
{
    return pageIndex == o.pageIndex && pixelWidth == o.pixelWidth && pixelHeight == o.pixelHeight;
}

// ---------------------------------------------------------------------------
// PdfBitmapCache
// ---------------------------------------------------------------------------

PdfBitmapCache::PdfBitmapCache(std::shared_ptr<PdfSharedDocument> pDoc)
    : m_pDoc(std::move(pDoc))
{
    m_PrefetchThread = std::thread(&PdfBitmapCache::prefetchLoop, this);
}

PdfBitmapCache::~PdfBitmapCache()
{
    cancelPrefetch();
}

void PdfBitmapCache::addToCache(const RenderKey& rKey, const BitmapEx& rBitmap)
{
    // Caller must hold m_Mutex
    size_t nMemSize = static_cast<size_t>(rBitmap.GetSizePixel().Width())
                    * static_cast<size_t>(rBitmap.GetSizePixel().Height()) * 4;

    if (m_CurrentMemory + nMemSize > MAX_MEMORY)
        evictLRU(nMemSize >= MAX_MEMORY ? 0 : (MAX_MEMORY - nMemSize));

    m_LRUList.push_front(rKey);
    m_Cache[rKey] = { rBitmap, nMemSize, m_LRUList.begin() };
    m_CurrentMemory += nMemSize;

    // Increment generation so PdfPagePrimitive2D::operator== detects the change
    ++m_nRenderGeneration;
}

BitmapEx PdfBitmapCache::getOrRender(int nPage, const Size& rPixelSize)
{
    // Align to 64-pixel grid to reduce cache thrashing during smooth zoom.
    // Zooming from e.g. 300% to 301% changes the pixel size by a few pixels,
    // but after alignment they map to the same cache key.
    constexpr int kAlign = 64;
    int nW = ((rPixelSize.Width()  + kAlign - 1) / kAlign) * kAlign;
    int nH = ((rPixelSize.Height() + kAlign - 1) / kAlign) * kAlign;
    RenderKey aKey{ nPage, nW, nH };

    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);

        // 1. Exact match in cache → return immediately
        auto it = m_Cache.find(aKey);
        if (it != m_Cache.end())
        {
            // LRU: move to front in O(1) using stored iterator
            m_LRUList.splice(m_LRUList.begin(), m_LRUList, it->second.lruIt);
            return it->second.bitmap;
        }

        // 2. Queue a background render at the exact size (if not already pending)
        if (m_PendingSet.find(aKey) == m_PendingSet.end())
        {
            m_PendingSet.insert(aKey);
            m_PendingQueue.push(aKey);
            m_Condition.notify_one();
        }

        // 3. Return a scaled placeholder from any cached bitmap for this page
        for (auto& [key, cached] : m_Cache)
        {
            if (key.pageIndex == nPage)
            {
                BitmapEx aScaled = cached.bitmap;
                aScaled.Scale(rPixelSize, BmpScaleFlag::Fast);
                return aScaled;
            }
        }
    }

    // 4. Nothing cached at all — render synchronously (first display of this page)
    BitmapEx aBitmap = m_pDoc->renderPage(nPage, rPixelSize);
    if (aBitmap.IsEmpty())
        return aBitmap;

    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);
        // Remove from pending since we just rendered it synchronously
        m_PendingSet.erase(aKey);
        // Remove from queue is O(n) so just leave it; the loop will skip cached entries
        addToCache(aKey, aBitmap);
    }

    return aBitmap;
}

int PdfBitmapCache::addRepaintCallback(std::function<void()> aCallback)
{
    std::lock_guard<std::mutex> aGuard(m_Mutex);
    int nId = m_nNextCallbackId++;
    m_aRepaintCallbacks[nId] = std::move(aCallback);
    return nId;
}

void PdfBitmapCache::removeRepaintCallback(int nId)
{
    std::lock_guard<std::mutex> aGuard(m_Mutex);
    m_aRepaintCallbacks.erase(nId);
}

void PdfBitmapCache::postRepaintEvent()
{
    std::vector<std::function<void()>> aCallbacks;
    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);
        for (auto& [id, fn] : m_aRepaintCallbacks)
            aCallbacks.push_back(fn);
    }

    if (aCallbacks.empty())
        return;

    auto* pData = new RepaintEventData{ std::move(aCallbacks) };
    ImplSVEvent* pEvent = Application::PostUserEvent(
        LINK_NONMEMBER(nullptr, invokeRepaintCallbacks), pData);
    if (!pEvent)
        delete pData; // event posting failed, clean up
}

void PdfBitmapCache::evictLRU(size_t targetMemory)
{
    while (m_CurrentMemory > targetMemory && !m_LRUList.empty())
    {
        const RenderKey& rKey = m_LRUList.back();
        auto it = m_Cache.find(rKey);
        if (it != m_Cache.end())
        {
            m_CurrentMemory -= it->second.memorySize;
            m_Cache.erase(it);
        }
        m_LRUList.pop_back();
    }
}

void PdfBitmapCache::prefetchFirstPages(int nCount)
{
    std::lock_guard<std::mutex> aGuard(m_Mutex);
    m_nPrefetchEndPage = nCount;
    m_Condition.notify_one();
}

void PdfBitmapCache::cancelPrefetch()
{
    m_bRunning = false;
    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);
        m_nPrefetchEndPage = 0;
    }
    m_Condition.notify_all();
    if (m_PrefetchThread.joinable())
        m_PrefetchThread.join();
}

void PdfBitmapCache::prefetchLoop()
{
    constexpr double kRenderDPI = 200.0;
    int nNextPrefetchPage = 0;

    while (m_bRunning)
    {
        RenderKey aPendingKey{ -1, 0, 0 };
        int nPrefetchEnd = 0;

        {
            std::unique_lock<std::mutex> aLock(m_Mutex);
            m_Condition.wait(aLock, [this, nNextPrefetchPage] {
                return !m_PendingQueue.empty()
                    || m_nPrefetchEndPage > nNextPrefetchPage
                    || !m_bRunning;
            });

            if (!m_bRunning)
                break;

            // Prioritize on-demand (zoom) renders over prefetch
            if (!m_PendingQueue.empty())
            {
                aPendingKey = m_PendingQueue.front();
                m_PendingQueue.pop();
                // Leave in m_PendingSet until after render so getOrRender
                // doesn't re-queue the same key while we're rendering it.
            }
            nPrefetchEnd = m_nPrefetchEndPage;
        }

        if (aPendingKey.pageIndex >= 0)
        {
            // Check if already cached (may have been rendered synchronously)
            {
                std::lock_guard<std::mutex> aGuard(m_Mutex);
                if (m_Cache.find(aPendingKey) != m_Cache.end())
                {
                    m_PendingSet.erase(aPendingKey);
                    continue;
                }
            }

            Size aSize(aPendingKey.pixelWidth, aPendingKey.pixelHeight);
            BitmapEx aBitmap = m_pDoc->renderPage(aPendingKey.pageIndex, aSize);

            {
                std::lock_guard<std::mutex> aGuard(m_Mutex);
                m_PendingSet.erase(aPendingKey);
                if (!aBitmap.IsEmpty())
                    addToCache(aPendingKey, aBitmap);
            }

            if (!aBitmap.IsEmpty())
                postRepaintEvent();
        }
        else if (nNextPrefetchPage < nPrefetchEnd)
        {
            // Prefetch at fixed DPI
            basegfx::B2DSize aPageSize = m_pDoc->getPageSize(nNextPrefetchPage);
            int nW = static_cast<int>(std::round(aPageSize.getWidth() * kRenderDPI / 72.0));
            int nH = static_cast<int>(std::round(aPageSize.getHeight() * kRenderDPI / 72.0));
            Size aPixelSize(std::max(1, nW), std::max(1, nH));

            RenderKey aPrefetchKey{ nNextPrefetchPage, static_cast<int>(aPixelSize.Width()), static_cast<int>(aPixelSize.Height()) };

            {
                std::lock_guard<std::mutex> aGuard(m_Mutex);
                if (m_Cache.find(aPrefetchKey) != m_Cache.end())
                {
                    ++nNextPrefetchPage;
                    continue;
                }
            }

            BitmapEx aBitmap = m_pDoc->renderPage(nNextPrefetchPage, aPixelSize);

            {
                std::lock_guard<std::mutex> aGuard(m_Mutex);
                if (!aBitmap.IsEmpty())
                    addToCache(aPrefetchKey, aBitmap);
            }

            // Notify the view to repaint if the prefetched page is visible
            if (!aBitmap.IsEmpty())
                postRepaintEvent();

            ++nNextPrefetchPage;
        }
    }
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
