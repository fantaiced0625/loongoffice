/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PdfPagePrimitive2D.hxx"
#include "PdfSharedDocument.hxx"
#include "PdfBitmapCache.hxx"

#include <drawinglayer/primitive2d/bitmapprimitive2d.hxx>
#include <drawinglayer/primitive2d/drawinglayer_primitivetypes2d.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>

namespace sd {

PdfPagePrimitive2D::PdfPagePrimitive2D(
    std::shared_ptr<PdfSharedDocument> pDoc,
    std::shared_ptr<PdfBitmapCache> pCache,
    int nPageIndex,
    const basegfx::B2DHomMatrix& rObjectTransform,
    const basegfx::B2DRange& rObjectRange)
    : drawinglayer::primitive2d::ObjectAndViewTransformationDependentPrimitive2D()
    , m_pDoc(std::move(pDoc))
    , m_pCache(std::move(pCache))
    , m_nPageIndex(nPageIndex)
    , m_aObjectTransform(rObjectTransform)
    , m_aObjectRange(rObjectRange)
{
}

drawinglayer::primitive2d::Primitive2DReference
PdfPagePrimitive2D::create2DDecomposition(const drawinglayer::geometry::ViewInformation2D& /*rViewInformation*/) const
{
    if (!m_pCache)
        return nullptr;

    // Calculate pixel size from view transformation.
    // getViewTransformation() maps from object (mm100) to view (pixels).
    // Transform the object size vector to get the required pixel dimensions.
    const basegfx::B2DVector aWorldSize(m_aObjectRange.getWidth(), m_aObjectRange.getHeight());
    const basegfx::B2DVector aViewSize = getViewTransformation() * aWorldSize;
    int nPixelWidth = static_cast<int>(std::round(std::abs(aViewSize.getX())));
    int nPixelHeight = static_cast<int>(std::round(std::abs(aViewSize.getY())));

    // Clamp to reasonable bounds
    if (nPixelWidth < 1)  nPixelWidth = 1;
    if (nPixelWidth > 8192) nPixelWidth = 8192;
    if (nPixelHeight < 1)  nPixelHeight = 1;
    if (nPixelHeight > 8192) nPixelHeight = 8192;

    // Get rendered bitmap at the exact resolution needed for this view
    BitmapEx aBitmapEx = m_pCache->getOrRender(m_nPageIndex, Size(nPixelWidth, nPixelHeight));

    if (aBitmapEx.IsEmpty())
        return nullptr;

    // Create a BitmapPrimitive2D at the object transform
    return drawinglayer::primitive2d::Primitive2DReference(
        new drawinglayer::primitive2d::BitmapPrimitive2D(aBitmapEx, m_aObjectTransform));
}

bool PdfPagePrimitive2D::operator==(const drawinglayer::primitive2d::BasePrimitive2D& rPrimitive) const
{
    if (this == &rPrimitive)
        return true;
    if (getPrimitive2DID() != rPrimitive.getPrimitive2DID())
        return false;
    const auto& rOther = static_cast<const PdfPagePrimitive2D&>(rPrimitive);
    return m_nPageIndex == rOther.m_nPageIndex
        && m_aObjectTransform == rOther.m_aObjectTransform
        && m_aObjectRange == rOther.m_aObjectRange;
}

basegfx::B2DRange PdfPagePrimitive2D::getB2DRange(const drawinglayer::geometry::ViewInformation2D& /*rViewInformation*/) const
{
    return m_aObjectRange;
}

sal_uInt32 PdfPagePrimitive2D::getPrimitive2DID() const
{
    return PRIMITIVE2D_ID_RANGE_SD | 0;
}

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
