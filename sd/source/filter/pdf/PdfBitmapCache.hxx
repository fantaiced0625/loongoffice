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
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

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

    BitmapEx getOrRender(int nPage, const Size& rPixelSize);
    void prefetchFirstPages(int nCount);
    void cancelPrefetch();

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

    // Background prefetch — render pages sequentially from 0 upward
    std::thread m_PrefetchThread;
    int m_nPrefetchEndPage = 0;
    std::condition_variable m_Condition;
    std::atomic<bool> m_bRunning{true};

    void prefetchLoop();
    void evictLRU(size_t targetMemory);
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
