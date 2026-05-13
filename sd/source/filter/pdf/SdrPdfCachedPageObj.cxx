/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SdrPdfCachedPageObj.hxx"
#include "ViewContactOfPdfCachedPage.hxx"
#include <svx/sdr/contact/viewcontact.hxx>

namespace sd {

SdrPdfCachedPageObj::SdrPdfCachedPageObj(
    SdrModel& rModel,
    std::shared_ptr<PdfSharedDocument> pDoc,
    std::shared_ptr<PdfBitmapCache> pCache,
    int nPageIndex,
    const tools::Rectangle& rRect)
    : SdrRectObj(rModel, SdrObjKind::Graphic, rRect)
    , m_pSharedDoc(std::move(pDoc))
    , m_pCache(std::move(pCache))
    , m_nPageIndex(nPageIndex)
{
    if (m_pCache)
        m_nRepaintCallbackId = m_pCache->addRepaintCallback([this]() { ActionChanged(); });
}

SdrPdfCachedPageObj::~SdrPdfCachedPageObj()
{
    if (m_pCache && m_nRepaintCallbackId >= 0)
        m_pCache->removeRepaintCallback(m_nRepaintCallbackId);
}

std::unique_ptr<sdr::contact::ViewContact> SdrPdfCachedPageObj::CreateObjectSpecificViewContact()
{
    return std::make_unique<ViewContactOfPdfCachedPage>(*this);
}

std::unique_ptr<sdr::properties::BaseProperties> SdrPdfCachedPageObj::CreateObjectSpecificProperties()
{
    return SdrRectObj::CreateObjectSpecificProperties();
}

SdrObjKind SdrPdfCachedPageObj::GetObjIdentifier() const
{
    return SdrObjKind::Graphic;
}

rtl::Reference<SdrObject> SdrPdfCachedPageObj::CloneSdrObject(SdrModel& rModel) const
{
    rtl::Reference<SdrPdfCachedPageObj> pClone = new SdrPdfCachedPageObj(
        rModel, m_pSharedDoc, m_pCache, m_nPageIndex,
        tools::Rectangle(Point(), GetLogicRect().GetSize()));
    return pClone;
}

BitmapEx SdrPdfCachedPageObj::getPageBitmap(const Size& rPixelSize) const
{
    if (!m_pCache)
        return BitmapEx();
    return m_pCache->getOrRender(m_nPageIndex, rPixelSize);
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
