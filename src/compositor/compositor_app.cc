// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/compositor_app.h"

#include "apps/mozart/src/compositor/compositor_impl.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace compositor {

CompositorApp::CompositorApp() {}

CompositorApp::~CompositorApp() {}

void CompositorApp::OnInitialize() {
  engine_.reset(new CompositorEngine());
}

bool CompositorApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<mozart::Compositor>(
      [this](const mojo::ConnectionContext& connection_context,
             mojo::InterfaceRequest<mozart::Compositor> compositor_request) {
        compositor_bindings_.AddBinding(new CompositorImpl(engine_.get()),
                                        compositor_request.Pass());
      });
  return true;
}

}  // namespace compositor
