// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/glue/skia/gl/ganesh_texture_surface.h"

#include <GLES2/gl2.h>

#include "apps/mozart/glue/gl/gl_texture.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"

namespace mojo {
namespace skia {

GaneshTextureSurface::GaneshTextureSurface(const GaneshContext::Scope& scope,
                                           std::unique_ptr<GLTexture> texture)
    : texture_(std::move(texture)) {
  FTL_DCHECK(texture_);
  FTL_DCHECK(texture_->texture_id());
  FTL_DCHECK(texture_->size().width > 0);
  FTL_DCHECK(texture_->size().height > 0);
  GrGLTextureInfo info;
  info.fTarget = GL_TEXTURE_2D;
  info.fID = texture_->texture_id();

  GrBackendTextureDesc desc;
  desc.fFlags = kRenderTarget_GrBackendTextureFlag;
  desc.fOrigin = kTopLeft_GrSurfaceOrigin;
  desc.fWidth = texture_->size().width;
  desc.fHeight = texture_->size().height;
  desc.fConfig = kSkia8888_GrPixelConfig;
  desc.fSampleCnt = 0;
  desc.fTextureHandle = reinterpret_cast<GrBackendObject>(&info);

  surface_ = SkSurface::MakeFromBackendTexture(scope.gr_context().get(), desc,
                                               nullptr);
  FTL_DCHECK(surface_);
}

GaneshTextureSurface::~GaneshTextureSurface() {}

std::unique_ptr<GLTexture> GaneshTextureSurface::TakeTexture() {
  surface_->getCanvas()->flush();
  surface_.reset();
  return std::move(texture_);
}

}  // namespace skia
}  // namespace mojo
