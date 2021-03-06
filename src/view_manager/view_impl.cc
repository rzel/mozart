// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_impl.h"

#include "apps/mozart/src/view_manager/view_registry.h"
#include "apps/mozart/src/view_manager/view_state.h"

namespace view_manager {

ViewImpl::ViewImpl(ViewRegistry* registry, ViewState* state)
    : registry_(registry), state_(state) {}

ViewImpl::~ViewImpl() {}

void ViewImpl::GetToken(const mozart::View::GetTokenCallback& callback) {
  callback.Run(state_->view_token()->Clone());
}

void ViewImpl::GetServiceProvider(
    mojo::InterfaceRequest<mojo::ServiceProvider> service_provider_request) {
  service_provider_bindings_.AddBinding(this, service_provider_request.Pass());
}

void ViewImpl::CreateScene(mojo::InterfaceRequest<mozart::Scene> scene) {
  registry_->CreateScene(state_, scene.Pass());
}

void ViewImpl::GetContainer(
    mojo::InterfaceRequest<mozart::ViewContainer> view_container_request) {
  container_bindings_.AddBinding(this, view_container_request.Pass());
}

void ViewImpl::Invalidate() {
  registry_->Invalidate(state_);
}

void ViewImpl::SetListener(
    mojo::InterfaceHandle<mozart::ViewContainerListener> listener) {
  state_->set_view_container_listener(
      mozart::ViewContainerListenerPtr::Create(std::move(listener)));
}

void ViewImpl::AddChild(
    uint32_t child_key,
    mojo::InterfaceHandle<mozart::ViewOwner> child_view_owner) {
  registry_->AddChild(state_, child_key, child_view_owner.Pass());
}

void ViewImpl::RemoveChild(
    uint32_t child_key,
    mojo::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  registry_->RemoveChild(state_, child_key,
                         transferred_view_owner_request.Pass());
}

void ViewImpl::SetChildProperties(
    uint32_t child_key,
    uint32_t child_scene_version,
    mozart::ViewPropertiesPtr child_view_properties) {
  registry_->SetChildProperties(state_, child_key, child_scene_version,
                                child_view_properties.Pass());
}

void ViewImpl::FlushChildren(uint32_t flush_token) {
  registry_->FlushChildren(state_, flush_token);
}

void ViewImpl::ConnectToService(const mojo::String& service_name,
                                mojo::ScopedMessagePipeHandle client_handle) {
  registry_->ConnectToViewService(state_, service_name, client_handle.Pass());
}

}  // namespace view_manager
