/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PdfSharedDocument.hxx"

#include <unotools/ucbstreamhelper.hxx>
#include <sal/log.hxx>

namespace sd {

bool PdfSharedDocument::loadFromFile(const OUString& rUrl)
{
    m_pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!m_pPdfium)
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: no PDFium library");
        return false;
    }

    std::unique_ptr<SvStream> xStream(
        ::utl::UcbStreamHelper::CreateStream(rUrl, StreamMode::READ | StreamMode::SHARE_DENYNONE));
    if (!xStream)
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: cannot open stream");
        return false;
    }

    // Read entire file into memory
    sal_uInt64 nSize = xStream->remainingSize();
    if (nSize <= 0 || nSize > 1024 * 1024 * 1024) // max 1 GB
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: invalid file size");
        return false;
    }

    m_aDataBuffer.resize(nSize);
    sal_uInt64 nRead = xStream->ReadBytes(m_aDataBuffer.data(), nSize);
    if (nRead != nSize)
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: failed to read entire file");
        return false;
    }

    m_pDoc = m_pPdfium->openDocument(m_aDataBuffer.data(), static_cast<int>(nSize), OString());
    if (!m_pDoc)
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: cannot open PDF document");
        return false;
    }

    int nPageCount = m_pDoc->getPageCount();
    if (nPageCount <= 0)
    {
        SAL_WARN("sd.pdf", "PdfSharedDocument::loadFromFile: document has no pages");
        return false;
    }

    m_aPageSizes.reserve(nPageCount);
    for (int i = 0; i < nPageCount; ++i)
    {
        m_aPageSizes.push_back(m_pDoc->getPageSize(i));
    }

    return true;
}

int PdfSharedDocument::getPageCount() const
{
    return static_cast<int>(m_aPageSizes.size());
}

basegfx::B2DSize PdfSharedDocument::getPageSize(int nPage) const
{
    if (nPage < 0 || nPage >= static_cast<int>(m_aPageSizes.size()))
        return basegfx::B2DSize();
    return m_aPageSizes[nPage];
}

BitmapEx PdfSharedDocument::renderPage(int nPage, const Size& rPixelSize)
{
    std::lock_guard<std::mutex> aGuard(m_Mutex);

    if (!m_pDoc || !m_pPdfium)
        return BitmapEx();

    if (nPage < 0 || nPage >= static_cast<int>(m_aPageSizes.size()))
        return BitmapEx();

    std::unique_ptr<vcl::pdf::PDFiumPage> pPage = m_pDoc->openPage(nPage);
    if (!pPage)
        return BitmapEx();

    int nWidth = rPixelSize.Width();
    int nHeight = rPixelSize.Height();
    if (nWidth <= 0 || nHeight <= 0)
        return BitmapEx();

    std::unique_ptr<vcl::pdf::PDFiumBitmap> pPdfBitmap
        = m_pPdfium->createBitmap(nWidth, nHeight, /*nAlpha=*/1);
    if (!pPdfBitmap)
        return BitmapEx();

    bool bTransparent = pPage->hasTransparency();
    const sal_uInt32 nColor = bTransparent ? 0x00000000 : 0xFFFFFFFF;
    pPdfBitmap->fillRect(0, 0, nWidth, nHeight, nColor);
    pPdfBitmap->renderPageBitmap(m_pDoc.get(), pPage.get(), /*nStartX=*/0, /*nStartY=*/0, nWidth, nHeight);
    return pPdfBitmap->createBitmapFromBuffer();
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
