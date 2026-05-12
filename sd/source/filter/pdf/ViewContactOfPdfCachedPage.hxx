/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <svx/sdr/contact/viewcontactofsdrobj.hxx>

namespace sd {

class SdrPdfCachedPageObj;

class ViewContactOfPdfCachedPage final : public sdr::contact::ViewContactOfSdrObj {
public:
    explicit ViewContactOfPdfCachedPage(SdrPdfCachedPageObj& rObj);
    virtual ~ViewContactOfPdfCachedPage() override;

protected:
    virtual void createViewIndependentPrimitive2DSequence(
        drawinglayer::primitive2d::Primitive2DDecompositionVisitor& rVisitor) const override;
};

} // namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
