/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <drawinglayer/primitive2d/primitivetools2d.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/range/b2drange.hxx>
#include <memory>

namespace sd {

class PdfSharedDocument;
class PdfBitmapCache;

/** A view-dependent primitive that re-renders the PDF page at the exact
    resolution needed for the current view transformation.

    This gives Okular-like behavior: when zooming in, the page is re-rendered
    at higher resolution instead of scaling a fixed-resolution bitmap.
 */
class PdfPagePrimitive2D final : public drawinglayer::primitive2d::ObjectAndViewTransformationDependentPrimitive2D
{
private:
    std::shared_ptr<PdfSharedDocument> m_pDoc;
    std::shared_ptr<PdfBitmapCache>    m_pCache;
    int                                m_nPageIndex;
    int                                m_nRenderGeneration;
    basegfx::B2DHomMatrix              m_aObjectTransform;
    basegfx::B2DRange                  m_aObjectRange;

    virtual drawinglayer::primitive2d::Primitive2DReference
        create2DDecomposition(const drawinglayer::geometry::ViewInformation2D& rViewInformation) const override;

public:
    PdfPagePrimitive2D(
        std::shared_ptr<PdfSharedDocument> pDoc,
        std::shared_ptr<PdfBitmapCache> pCache,
        int nPageIndex,
        const basegfx::B2DHomMatrix& rObjectTransform,
        const basegfx::B2DRange& rObjectRange);

    virtual bool operator==(const drawinglayer::primitive2d::BasePrimitive2D& rPrimitive) const override;
    virtual basegfx::B2DRange getB2DRange(const drawinglayer::geometry::ViewInformation2D& rViewInformation) const override;
    virtual sal_uInt32 getPrimitive2DID() const override;
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
