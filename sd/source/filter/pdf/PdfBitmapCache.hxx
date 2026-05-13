/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <vcl/bitmapex.hxx>
#include <tools/gen.hxx>
#include <memory>
#include <map>
#include <list>
#include <set>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

namespace sd {

class PdfSharedDocument;

struct RenderKey {
    int pageIndex;
    int pixelWidth;
    int pixelHeight;

    bool operator<(const RenderKey& o) const;
    bool operator==(const RenderKey& o) const;
};

class PdfBitmapCache {
public:
    explicit PdfBitmapCache(std::shared_ptr<PdfSharedDocument> pDoc);
    ~PdfBitmapCache();

    // Non-blocking: returns cached bitmap or scaled placeholder immediately.
    // If exact size is not cached, queues a background render and returns a
    // scaled version of any existing cached bitmap for the page (or renders
    // synchronously if nothing is cached yet).
    BitmapEx getOrRender(int nPage, const Size& rPixelSize);

    void prefetchFirstPages(int nCount);
    void cancelPrefetch();

    // Register a callback to be invoked (on the main thread) when a
    // background render completes. Returns an ID for later removal.
    int addRepaintCallback(std::function<void()> aCallback);
    void removeRepaintCallback(int nId);

    // Returns the current render generation counter.
    // Incremented each time a new bitmap is added to the cache.
    int getRenderGeneration() const { return m_nRenderGeneration.load(); }

private:
    struct CachedBitmap {
        BitmapEx bitmap;
        size_t memorySize;
    };

    static constexpr size_t MAX_MEMORY = 100 * 1024 * 1024; // 100 MB
    std::shared_ptr<PdfSharedDocument> m_pDoc;
    std::map<RenderKey, CachedBitmap> m_Cache;
    std::list<RenderKey> m_LRUList;
    size_t m_CurrentMemory = 0;
    std::mutex m_Mutex;

    // Background thread — handles both prefetch and on-demand (zoom) renders
    std::thread m_PrefetchThread;
    int m_nPrefetchEndPage = 0;
    std::condition_variable m_Condition;
    std::atomic<bool> m_bRunning{true};

    // On-demand render queue (zoom changes)
    std::queue<RenderKey> m_PendingQueue;
    std::set<RenderKey>   m_PendingSet;

    // Render generation: incremented each time a bitmap is added to cache.
    // Used by PdfPagePrimitive2D::operator== to detect stale decompositions.
    std::atomic<int> m_nRenderGeneration{0};

    // Repaint callbacks (called on main thread after background render)
    std::map<int, std::function<void()>> m_aRepaintCallbacks;
    int m_nNextCallbackId = 0;

    void prefetchLoop();
    void evictLRU(size_t targetMemory);
    void addToCache(const RenderKey& rKey, const BitmapEx& rBitmap);
    void postRepaintEvent();
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
