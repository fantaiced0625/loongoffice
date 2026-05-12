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

namespace sd {

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

PdfBitmapCache::PdfBitmapCache(std::shared_ptr<PdfSharedDocument> pDoc)
    : m_pDoc(std::move(pDoc))
{
    m_PrefetchThread = std::thread(&PdfBitmapCache::prefetchLoop, this);
}

PdfBitmapCache::~PdfBitmapCache()
{
    cancelPrefetch();
}

BitmapEx PdfBitmapCache::getOrRender(int nPage, const Size& rPixelSize)
{
    RenderKey aKey{ nPage, rPixelSize.Width(), rPixelSize.Height() };

    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);

        // Check cache
        auto it = m_Cache.find(aKey);
        if (it != m_Cache.end())
        {
            // LRU: move to front
            m_LRUList.remove(aKey);
            m_LRUList.push_front(aKey);
            return it->second.bitmap;
        }
    }

    // Not in cache, render
    BitmapEx aBitmap = m_pDoc->renderPage(nPage, rPixelSize);
    if (aBitmap.IsEmpty())
        return aBitmap;

    // Add to cache
    {
        std::lock_guard<std::mutex> aGuard(m_Mutex);

        size_t nMemSize = aBitmap.GetSizePixel().Width() * aBitmap.GetSizePixel().Height() * 4;
        CachedBitmap aCached{ aBitmap, nMemSize };

        // Evict if needed
        if (m_CurrentMemory + nMemSize > MAX_MEMORY)
        {
            evictLRU(MAX_MEMORY - nMemSize);
        }

        m_Cache[aKey] = aCached;
        m_LRUList.push_front(aKey);
        m_CurrentMemory += nMemSize;
    }

    return aBitmap;
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
    // DPI constant must match ViewContactOfPdfCachedPage::createViewIndependentPrimitive2DSequence
    constexpr double kRenderDPI = 200.0;

    int nNextPage = 0;
    while (m_bRunning)
    {
        int nEndPage = 0;
        {
            std::unique_lock<std::mutex> aLock(m_Mutex);
            m_Condition.wait(aLock, [this, nNextPage] {
                return m_nPrefetchEndPage > nNextPage || !m_bRunning;
            });

            if (!m_bRunning)
                break;

            nEndPage = m_nPrefetchEndPage;
        }

        // Render pages sequentially from nNextPage to nEndPage-1
        while (nNextPage < nEndPage && m_bRunning)
        {
            // Calculate pixel size at render DPI (must match ViewContact formula)
            basegfx::B2DSize aPageSize = m_pDoc->getPageSize(nNextPage);
            int nW = static_cast<int>(std::round(aPageSize.getWidth() * kRenderDPI / 72.0));
            int nH = static_cast<int>(std::round(aPageSize.getHeight() * kRenderDPI / 72.0));
            Size aPixelSize(std::max(1, nW), std::max(1, nH));

            // getOrRender will render if not in cache; the result is cached
            getOrRender(nNextPage, aPixelSize);
            ++nNextPage;
        }
    }
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
