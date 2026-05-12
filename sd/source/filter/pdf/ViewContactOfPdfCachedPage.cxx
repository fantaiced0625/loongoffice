/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ViewContactOfPdfCachedPage.hxx"
#include "SdrPdfCachedPageObj.hxx"
#include "PdfPagePrimitive2D.hxx"

#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <vcl/canvastools.hxx>

namespace sd {

ViewContactOfPdfCachedPage::ViewContactOfPdfCachedPage(SdrPdfCachedPageObj& rObj)
    : sdr::contact::ViewContactOfSdrObj(rObj)
{
}

ViewContactOfPdfCachedPage::~ViewContactOfPdfCachedPage() = default;

void ViewContactOfPdfCachedPage::createViewIndependentPrimitive2DSequence(
    drawinglayer::primitive2d::Primitive2DDecompositionVisitor& rVisitor) const
{
    SdrPdfCachedPageObj& rObj = static_cast<SdrPdfCachedPageObj&>(mrObject);

    const tools::Rectangle aRectangle(rObj.GetGeoRect());
    const basegfx::B2DRange aObjectRange = vcl::unotools::b2DRectangleFromRectangle(aRectangle);

    // Build object transform: scale from unit square to object geometry
    basegfx::B2DHomMatrix aTransform = basegfx::utils::createScaleTranslateB2DHomMatrix(
        aObjectRange.getWidth(), aObjectRange.getHeight(),
        aObjectRange.getMinX(), aObjectRange.getMinY());

    // Use PdfPagePrimitive2D: re-renders at the exact resolution needed
    // for the current view (Okular-like zoom behavior)
    const drawinglayer::primitive2d::Primitive2DReference xReference(
        new PdfPagePrimitive2D(
            rObj.getSharedDoc(), rObj.getCache(),
            rObj.getPageIndex(), aTransform, aObjectRange));
    rVisitor.visit(xReference);
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
