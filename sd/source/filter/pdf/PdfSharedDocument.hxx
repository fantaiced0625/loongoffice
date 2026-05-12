/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <vcl/filter/PDFiumLibrary.hxx>
#include <basegfx/vector/b2dsize.hxx>
#include <vcl/bitmapex.hxx>
#include <tools/gen.hxx>
#include <memory>
#include <mutex>
#include <vector>

namespace sd {

class PdfSharedDocument {
public:
    PdfSharedDocument() = default;
    ~PdfSharedDocument() = default;

    bool loadFromFile(const OUString& rUrl);
    int getPageCount() const;
    basegfx::B2DSize getPageSize(int nPage) const;

    // Render a single page to BitmapEx at specified pixel size
    BitmapEx renderPage(int nPage, const Size& rPixelSize);

private:
    std::shared_ptr<vcl::pdf::PDFium> m_pPdfium;
    std::unique_ptr<vcl::pdf::PDFiumDocument> m_pDoc;
    std::mutex m_Mutex;
    std::vector<basegfx::B2DSize> m_aPageSizes;
    std::vector<char> m_aDataBuffer; // Keep PDF binary data alive for PDFium
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
