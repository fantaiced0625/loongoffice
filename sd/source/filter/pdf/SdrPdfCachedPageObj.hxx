/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <svx/svdorect.hxx>
#include <memory>
#include "PdfSharedDocument.hxx"
#include "PdfBitmapCache.hxx"

namespace sd {

class SdrPdfCachedPageObj final : public SdrRectObj {
public:
    SdrPdfCachedPageObj(
        SdrModel& rModel,
        std::shared_ptr<PdfSharedDocument> pDoc,
        std::shared_ptr<PdfBitmapCache> pCache,
        int nPageIndex,
        const tools::Rectangle& rRect);

    virtual ~SdrPdfCachedPageObj() override;

    // Factory methods
    virtual std::unique_ptr<sdr::contact::ViewContact>
        CreateObjectSpecificViewContact() override;
    virtual std::unique_ptr<sdr::properties::BaseProperties>
        CreateObjectSpecificProperties() override;

    // SdrObjKind
    virtual SdrObjKind GetObjIdentifier() const override;
    virtual rtl::Reference<SdrObject> CloneSdrObject(SdrModel& rModel) const override;

    // Page bitmap access (called by ViewContact)
    BitmapEx getPageBitmap(const Size& rPixelSize) const;
    int getPageIndex() const { return m_nPageIndex; }
    std::shared_ptr<PdfSharedDocument> getSharedDoc() const { return m_pSharedDoc; }
    std::shared_ptr<PdfBitmapCache> getCache() const { return m_pCache; }

private:
    std::shared_ptr<PdfSharedDocument> m_pSharedDoc;
    std::shared_ptr<PdfBitmapCache> m_pCache;
    int m_nPageIndex;
    int m_nRepaintCallbackId = -1;
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
