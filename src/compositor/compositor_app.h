// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_
#define APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_

#include <memory>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/src/compositor/compositor_engine.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace compositor {

// Compositor application entry point.
class CompositorApp : public mojo::ApplicationImplBase {
 public:
  CompositorApp();
  ~CompositorApp() override;

 private:
  // |ApplicationImplBase|:
  void OnInitialize() override;
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override;

  mojo::StrongBindingSet<mozart::Compositor> compositor_bindings_;
  std::unique_ptr<CompositorEngine> engine_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CompositorApp);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_
