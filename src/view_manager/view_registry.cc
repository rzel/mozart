// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_registry.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/view_manager/view_impl.h"
#include "apps/mozart/src/view_manager/view_tree_impl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/strings/string_printf.h"

namespace view_manager {
namespace {
constexpr uint32_t kSceneResourceId = 1u;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

bool Validate(const mozart::DisplayMetrics& value) {
  return std::isnormal(value.device_pixel_ratio) &&
         value.device_pixel_ratio > 0.f;
}

bool Validate(const mozart::ViewLayout& value) {
  return value.size && value.size->width >= 0 && value.size->height >= 0;
}

bool Validate(const mozart::ViewProperties& value) {
  if (value.display_metrics && !Validate(*value.display_metrics))
    return false;
  if (value.view_layout && !Validate(*value.view_layout))
    return false;
  return true;
}

// Returns true if the properties are valid and are sufficient for
// operating the view tree.
bool IsComplete(const mozart::ViewProperties& value) {
  return Validate(value) && value.view_layout && value.display_metrics;
}

void ApplyOverrides(mozart::ViewProperties* value,
                    const mozart::ViewProperties* overrides) {
  if (!overrides)
    return;
  if (overrides->display_metrics)
    value->display_metrics = overrides->display_metrics.Clone();
  if (overrides->view_layout)
    value->view_layout = overrides->view_layout.Clone();
}

std::string SanitizeLabel(const mojo::String& label) {
  return label.get().substr(0, mozart::ViewManager::kLabelMaxLength);
}
}  // namespace

ViewRegistry::ViewRegistry(mozart::CompositorPtr compositor)
    : compositor_(compositor.Pass()) {}

ViewRegistry::~ViewRegistry() {}

// REGISTERING ASSOCIATES

void ViewRegistry::RegisterViewAssociate(
    mozart::ViewInspector* view_inspector,
    mozart::ViewAssociatePtr view_associate,
    mojo::InterfaceRequest<mozart::ViewAssociateOwner> view_associate_owner,
    const mojo::String& label) {
  associate_table_.RegisterViewAssociate(view_inspector, view_associate.Pass(),
                                         view_associate_owner.Pass(), label);
}

void ViewRegistry::FinishedRegisteringViewAssociates() {
  associate_table_.FinishedRegisteringViewAssociates();
};

// CREATE / DESTROY VIEWS

void ViewRegistry::CreateView(
    mojo::InterfaceRequest<mozart::View> view_request,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    mozart::ViewListenerPtr view_listener,
    const mojo::String& label) {
  FTL_DCHECK(view_request.is_pending());
  FTL_DCHECK(view_owner_request.is_pending());
  FTL_DCHECK(view_listener);

  auto view_token = mozart::ViewToken::New();
  view_token->value = next_view_token_value_++;
  FTL_CHECK(view_token->value);
  FTL_CHECK(!FindView(view_token->value));

  // Create the state and bind the interfaces to it.
  ViewState* view_state =
      new ViewState(this, view_token.Pass(), view_request.Pass(),
                    view_listener.Pass(), SanitizeLabel(label));
  view_state->BindOwner(view_owner_request.Pass());

  // Add to registry and return token.
  views_by_token_.emplace(view_state->view_token()->value, view_state);
  DVLOG(1) << "CreateView: view=" << view_state;
}

void ViewRegistry::OnViewDied(ViewState* view_state,
                              const std::string& reason) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(1) << "OnViewDied: view=" << view_state << ", reason=" << reason;

  UnregisterView(view_state);
}

void ViewRegistry::UnregisterView(ViewState* view_state) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(1) << "UnregisterView: view=" << view_state;

  HijackView(view_state);
  UnregisterChildren(view_state);

  // Remove from registry.
  if (view_state->scene_token())
    views_by_scene_token_.erase(view_state->scene_token()->value);
  views_by_token_.erase(view_state->view_token()->value);
  delete view_state;
}

// CREATE / DESTROY VIEW TREES

void ViewRegistry::CreateViewTree(
    mojo::InterfaceRequest<mozart::ViewTree> view_tree_request,
    mozart::ViewTreeListenerPtr view_tree_listener,
    const mojo::String& label) {
  FTL_DCHECK(view_tree_request.is_pending());
  FTL_DCHECK(view_tree_listener);

  auto view_tree_token = mozart::ViewTreeToken::New();
  view_tree_token->value = next_view_tree_token_value_++;
  FTL_CHECK(view_tree_token->value);
  FTL_CHECK(!FindViewTree(view_tree_token->value));

  // Create the state and bind the interfaces to it.
  ViewTreeState* tree_state =
      new ViewTreeState(this, view_tree_token.Pass(), view_tree_request.Pass(),
                        view_tree_listener.Pass(), SanitizeLabel(label));

  // Add to registry.
  view_trees_by_token_.emplace(tree_state->view_tree_token()->value,
                               tree_state);
  DVLOG(1) << "CreateViewTree: tree=" << tree_state;
}

void ViewRegistry::OnViewTreeDied(ViewTreeState* tree_state,
                                  const std::string& reason) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(1) << "OnViewTreeDied: tree=" << tree_state << ", reason=" << reason;

  UnregisterViewTree(tree_state);
}

void ViewRegistry::UnregisterViewTree(ViewTreeState* tree_state) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(1) << "UnregisterViewTree: tree=" << tree_state;

  UnregisterChildren(tree_state);

  // Remove from registry.
  view_trees_by_token_.erase(tree_state->view_tree_token()->value);
  delete tree_state;
}

// LIFETIME

void ViewRegistry::UnregisterViewContainer(
    ViewContainerState* container_state) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));

  ViewState* view_state = container_state->AsViewState();
  if (view_state)
    UnregisterView(view_state);
  else
    UnregisterViewTree(container_state->AsViewTreeState());
}

void ViewRegistry::UnregisterViewStub(std::unique_ptr<ViewStub> view_stub) {
  FTL_DCHECK(view_stub);

  ViewState* view_state = view_stub->ReleaseView();
  if (view_state)
    UnregisterView(view_state);
}

void ViewRegistry::UnregisterChildren(ViewContainerState* container_state) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));

  // Recursively unregister all children since they will become unowned
  // at this point taking care to unlink each one before its unregistration.
  for (auto& child : container_state->UnlinkAllChildren())
    UnregisterViewStub(std::move(child));
}

// SCENE MANAGEMENT

void ViewRegistry::CreateScene(ViewState* view_state,
                               mojo::InterfaceRequest<mozart::Scene> scene) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FTL_DCHECK(scene.is_pending());
  DVLOG(1) << "CreateScene: view=" << view_state;

  compositor_->CreateScene(scene.Pass(), view_state->label(), [
    this, weak = view_state->GetWeakPtr()
  ](mozart::SceneTokenPtr scene_token) {
    if (weak)
      OnViewSceneTokenAvailable(weak, scene_token.Pass());
  });
}

void ViewRegistry::OnViewSceneTokenAvailable(
    ftl::WeakPtr<ViewState> view_state_weak,
    mozart::SceneTokenPtr scene_token) {
  FTL_DCHECK(scene_token);
  ViewState* view_state = view_state_weak.get();
  if (!view_state)
    return;

  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(2) << "OnSceneCreated: view=" << view_state
           << ", scene_token=" << scene_token;

  if (view_state->scene_token())
    views_by_scene_token_.erase(view_state->scene_token()->value);
  views_by_scene_token_.emplace(scene_token->value, view_state);

  view_state->set_scene_token(scene_token.Pass());

  PublishStubScene(view_state);
}

void ViewRegistry::OnStubSceneTokenAvailable(
    ftl::WeakPtr<ViewStub> view_stub_weak,
    mozart::SceneTokenPtr scene_token) {
  FTL_DCHECK(scene_token);

  ViewStub* view_stub = view_stub_weak.get();
  if (!view_stub || view_stub->is_unavailable())
    return;

  DVLOG(2) << "OnStubSceneCreated: view_state=" << view_stub->state()
           << ", scene_token=" << scene_token;

  // Store the scene token.
  FTL_DCHECK(view_stub->is_linked());
  view_stub->SetStubSceneToken(scene_token.Clone());
  if (view_stub->state())
    PublishStubScene(view_stub->state());

  // Send view info to the container including the scene token.
  auto view_info = mozart::ViewInfo::New();
  view_info->scene_token = scene_token.Pass();
  if (view_stub->container()) {
    SendChildAttached(view_stub->container(), view_stub->key(),
                      view_info.Pass());
  }

  // If this is the root of the tree, update the renderer now that we
  // know the scene token.
  if (view_stub->is_root_of_tree())
    SetRendererRootScene(view_stub->tree());
}

void ViewRegistry::PublishStubScene(ViewState* view_state) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));

  if (!view_state->view_stub())
    return;

  FTL_DCHECK(
      view_state->view_stub()->stub_scene());  // we know view is attached
  DVLOG(2) << "PublishStubScene: view=" << view_state
           << ", view_stub=" << view_state->view_stub() << ", stub_scene_token="
           << view_state->view_stub()->stub_scene_token();

  auto update = mozart::SceneUpdate::New();
  update->clear_resources = true;
  update->clear_nodes = true;

  if (view_state->scene_token() && view_state->issued_properties()) {
    auto scene_resource = mozart::Resource::New();
    scene_resource->set_scene(mozart::SceneResource::New());
    scene_resource->get_scene()->scene_token =
        view_state->scene_token()->Clone();
    update->resources.insert(kSceneResourceId, scene_resource.Pass());

    const mozart::ViewLayoutPtr& layout =
        view_state->issued_properties()->view_layout;
    FTL_DCHECK(layout && layout->size);

    auto root_node = mozart::Node::New();
    root_node->content_clip = mojo::RectF::New();
    root_node->content_clip->width = layout->size->width;
    root_node->content_clip->height = layout->size->height;
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_scene(mozart::SceneNodeOp::New());
    root_node->op->get_scene()->scene_resource_id = kSceneResourceId;
    root_node->op->get_scene()->scene_version =
        view_state->issued_scene_version();
    update->nodes.insert(kRootNodeId, root_node.Pass());
  }
  view_state->view_stub()->stub_scene()->Update(update.Pass());

  auto metadata = mozart::SceneMetadata::New();
  metadata->version = view_state->view_stub()->scene_version();
  view_state->view_stub()->stub_scene()->Publish(metadata.Pass());

  if (view_state->view_stub()->is_root_of_tree())
    SetRendererRootScene(view_state->view_stub()->tree());
}

// RENDERING

void ViewRegistry::SetRenderer(ViewTreeState* tree_state,
                               mozart::RendererPtr renderer) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(1) << "SetRenderer: tree=" << tree_state;

  if (renderer) {
    renderer.set_connection_error_handler(
        [this, tree_state] { OnRendererDied(tree_state); });
  }

  tree_state->SetRenderer(renderer.Pass());
  ScheduleViewTreeInvalidation(tree_state,
                               ViewTreeState::INVALIDATION_RENDERER_CHANGED);
  SetRendererRootScene(tree_state);
}

void ViewRegistry::OnRendererDied(ViewTreeState* tree_state) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(1) << "OnRendererDied: tree=" << tree_state;
  FTL_DCHECK(tree_state->renderer());

  tree_state->SetRenderer(nullptr);
  ScheduleViewTreeInvalidation(tree_state,
                               ViewTreeState::INVALIDATION_RENDERER_CHANGED);
  SendRendererDied(tree_state);
}

void ViewRegistry::SetRendererRootScene(ViewTreeState* tree_state) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));

  if (!tree_state->renderer())
    return;

  // TODO(jeffbrown): Avoid sending the same information if already set.

  ViewStub* root_stub = tree_state->GetRoot();
  if (root_stub && root_stub->stub_scene_token() && root_stub->properties() &&
      IsComplete(*root_stub->properties())) {
    const mozart::ViewLayoutPtr& layout = root_stub->properties()->view_layout;
    FTL_DCHECK(layout && layout->size);

    auto viewport = mojo::Rect::New();
    viewport->width = layout->size->width;
    viewport->height = layout->size->height;
    DVLOG(2) << "SetRootScene: tree=" << tree_state
             << ", scene_token=" << root_stub->stub_scene_token()
             << ", scene_version=" << root_stub->scene_version()
             << ", viewport=" << viewport;
    tree_state->renderer()->SetRootScene(root_stub->stub_scene_token()->Clone(),
                                         root_stub->scene_version(),
                                         viewport.Pass());
    return;
  }

  DVLOG(2) << "ClearRootScene: tree=" << tree_state;
  tree_state->renderer()->ClearRootScene();
}

// TREE MANIPULATION

void ViewRegistry::AddChild(
    ViewContainerState* container_state,
    uint32_t child_key,
    mojo::InterfaceHandle<mozart::ViewOwner> child_view_owner) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FTL_DCHECK(child_view_owner);
  DVLOG(1) << "AddChild: container=" << container_state
           << ", child_key=" << child_key;

  // Ensure there are no other children with the same key.
  if (container_state->children().find(child_key) !=
      container_state->children().end()) {
    FTL_LOG(ERROR) << "Attempted to add a child with a duplicate key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // If this is a view tree, ensure it only has one root.
  ViewTreeState* view_tree_state = container_state->AsViewTreeState();
  if (view_tree_state && !container_state->children().empty()) {
    FTL_LOG(ERROR) << "Attempted to add a second child to a view tree: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // Add a stub, pending resolution of the view owner.
  // Assuming the stub isn't removed prematurely, |OnViewResolved| will be
  // called asynchronously with the result of the resolution.
  container_state->LinkChild(child_key, std::unique_ptr<ViewStub>(new ViewStub(
                                            this, child_view_owner.Pass())));
}

void ViewRegistry::RemoveChild(
    ViewContainerState* container_state,
    uint32_t child_key,
    mojo::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  DVLOG(1) << "RemoveChild: container=" << container_state
           << ", child_key=" << child_key;

  // Ensure the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FTL_LOG(ERROR) << "Attempted to remove a child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // Unlink the child from its container.
  TransferOrUnregisterViewStub(container_state->UnlinkChild(child_key),
                               transferred_view_owner_request.Pass());

  // If the root was removed, tell the renderer.
  ViewTreeState* tree_state = container_state->AsViewTreeState();
  if (tree_state)
    SetRendererRootScene(tree_state);
}

void ViewRegistry::SetChildProperties(
    ViewContainerState* container_state,
    uint32_t child_key,
    uint32_t child_scene_version,
    mozart::ViewPropertiesPtr child_properties) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  DVLOG(1) << "SetChildProperties: container=" << container_state
           << ", child_key=" << child_key
           << ", child_scene_version=" << child_scene_version
           << ", child_properties=" << child_properties;

  // Check whether the properties are well-formed.
  if (child_properties && !Validate(*child_properties)) {
    FTL_LOG(ERROR) << "Attempted to set invalid child view properties: "
                   << "container=" << container_state
                   << ", child_key=" << child_key
                   << ", child_scene_version=" << child_scene_version
                   << ", child_properties=" << child_properties;
    UnregisterViewContainer(container_state);
    return;
  }

  // Check whether the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FTL_LOG(ERROR) << "Attempted to modify child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key
                   << ", child_scene_version=" << child_scene_version
                   << ", child_properties=" << child_properties;
    UnregisterViewContainer(container_state);
    return;
  }

  // Immediately discard requests on unavailable views.
  ViewStub* child_stub = child_it->second.get();
  if (child_stub->is_unavailable())
    return;

  // Store the updated properties specified by the container if changed.
  if (child_scene_version == child_stub->scene_version() &&
      child_properties.Equals(child_stub->properties()))
    return;

  // Apply the change.
  child_stub->SetProperties(child_scene_version, child_properties.Pass());
  if (child_stub->state()) {
    ScheduleViewInvalidation(child_stub->state(),
                             ViewState::INVALIDATION_PROPERTIES_CHANGED);
  }
}

void ViewRegistry::FlushChildren(ViewContainerState* container_state,
                                 uint32_t flush_token) {
  FTL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  DVLOG(1) << "FlushChildren: container=" << container_state
           << ", flush_token=" << flush_token;
}

void ViewRegistry::OnViewResolved(ViewStub* view_stub,
                                  mozart::ViewTokenPtr view_token) {
  FTL_DCHECK(view_stub);

  ViewState* view_state = view_token ? FindView(view_token->value) : nullptr;
  if (view_state)
    AttachResolvedViewAndNotify(view_stub, view_state);
  else
    ReleaseUnavailableViewAndNotify(view_stub);
}

void ViewRegistry::TransferViewOwner(
    mozart::ViewTokenPtr view_token,
    mojo::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(transferred_view_owner_request.is_pending());

  ViewState* view_state = view_token ? FindView(view_token->value) : nullptr;
  if (view_state) {
    view_state->ReleaseOwner();  // don't need the ViewOwner pipe anymore
    view_state->BindOwner(transferred_view_owner_request.Pass());
  }
}

void ViewRegistry::AttachResolvedViewAndNotify(ViewStub* view_stub,
                                               ViewState* view_state) {
  FTL_DCHECK(view_stub);
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(2) << "AttachViewStubAndNotify: view=" << view_state;

  // Create the scene and get its token asynchronously.
  // TODO(jeffbrown): It would be really nice to have a way to pipeline
  // getting the scene token.
  mozart::ScenePtr stub_scene;
  compositor_->CreateScene(
      mojo::GetProxy(&stub_scene),
      ftl::StringPrintf("*%s", view_state->label().c_str()),
      [ this,
        weak = view_stub->GetWeakPtr() ](mozart::SceneTokenPtr scene_token) {
        if (weak)
          OnStubSceneTokenAvailable(weak, scene_token.Pass());
      });

  // Hijack the view from its current container, if needed.
  HijackView(view_state);

  // Attach the view.
  view_state->ReleaseOwner();  // don't need the ViewOwner pipe anymore
  view_stub->AttachView(view_state, stub_scene.Pass());
  ScheduleViewInvalidation(view_state, ViewState::INVALIDATION_PARENT_CHANGED);
}

void ViewRegistry::ReleaseUnavailableViewAndNotify(ViewStub* view_stub) {
  FTL_DCHECK(view_stub);
  DVLOG(2) << "ReleaseUnavailableViewAndNotify: key=" << view_stub->key();

  ViewState* view_state = view_stub->ReleaseView();
  FTL_DCHECK(!view_state);

  if (view_stub->container())
    SendChildUnavailable(view_stub->container(), view_stub->key());
}

void ViewRegistry::HijackView(ViewState* view_state) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));

  ViewStub* view_stub = view_state->view_stub();
  if (view_stub)
    ReleaseUnavailableViewAndNotify(view_stub);
}

void ViewRegistry::TransferOrUnregisterViewStub(
    std::unique_ptr<ViewStub> view_stub,
    mojo::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  FTL_DCHECK(view_stub);

  if (transferred_view_owner_request.is_pending()) {
    if (view_stub->state()) {
      ViewState* view_state = view_stub->ReleaseView();
      ScheduleViewInvalidation(view_state,
                               ViewState::INVALIDATION_PARENT_CHANGED);
      view_state->BindOwner(transferred_view_owner_request.Pass());
      return;
    }
    if (view_stub->is_pending()) {
      FTL_DCHECK(!view_stub->state());

      // Handle transfer of pending view.
      view_stub->TransferViewOwnerWhenViewResolved(
          std::move(view_stub), transferred_view_owner_request.Pass());

      return;
    }
  }
  UnregisterViewStub(std::move(view_stub));
}

// INVALIDATION

void ViewRegistry::Invalidate(ViewState* view_state) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(1) << "Invalidate: view=" << view_state;

  ScheduleViewInvalidation(view_state, ViewState::INVALIDATION_EXPLICIT);
}

void ViewRegistry::ScheduleViewInvalidation(ViewState* view_state,
                                            uint32_t flags) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(2) << "ScheduleViewInvalidation: view=" << view_state
           << ", flags=" << flags;

  view_state->set_invalidation_flags(view_state->invalidation_flags() | flags);
  if (view_state->view_stub() && view_state->view_stub()->tree()) {
    ScheduleViewTreeInvalidation(view_state->view_stub()->tree(),
                                 ViewTreeState::INVALIDATION_VIEWS_INVALIDATED);
  }
}

void ViewRegistry::ScheduleViewTreeInvalidation(ViewTreeState* tree_state,
                                                uint32_t flags) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(2) << "ScheduleViewTreeInvalidation: tree=" << tree_state
           << ", flags=" << flags;

  tree_state->set_invalidation_flags(tree_state->invalidation_flags() | flags);
  if (flags & ViewTreeState::INVALIDATION_RENDERER_CHANGED)
    tree_state->set_frame_scheduled(false);
  if (!tree_state->frame_scheduled() && tree_state->frame_scheduler()) {
    // It's safe to pass Unretained(tree_state) because the scheduler's
    // lifetime is bound to that of the view tree and its renderer so we can
    // only receive a callback if the tree still exists and has the same
    // renderer.
    tree_state->set_frame_scheduled(true);
    tree_state->frame_scheduler()->ScheduleFrame(
        [this, tree_state](mozart::FrameInfoPtr frame_info) {
          TraverseViewTree(tree_state, frame_info.Pass());
        });
  }
}

void ViewRegistry::TraverseViewTree(ViewTreeState* tree_state,
                                    mozart::FrameInfoPtr frame_info) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  DVLOG(2) << "TraverseViewTree: tree=" << tree_state
           << ", frame_info=" << frame_info
           << ", invalidation_flags=" << tree_state->invalidation_flags();
  FTL_DCHECK(tree_state->frame_scheduled());
  FTL_DCHECK(tree_state->invalidation_flags());

  tree_state->set_frame_scheduled(false);
  tree_state->set_invalidation_flags(0u);

  ViewStub* root_stub = tree_state->GetRoot();
  if (root_stub && root_stub->state())
    TraverseView(root_stub->state(), frame_info.get(), false);
}

void ViewRegistry::TraverseView(ViewState* view_state,
                                const mozart::FrameInfo* frame_info,
                                bool parent_properties_changed) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));
  DVLOG(2) << "TraverseView: view=" << view_state
           << ", frame_info=" << frame_info
           << ", parent_properties_changed=" << parent_properties_changed
           << ", invalidation_flags=" << view_state->invalidation_flags();

  uint32_t flags = view_state->invalidation_flags();

  // Update view properties.
  bool view_properties_changed = false;
  if (parent_properties_changed ||
      (flags & (ViewState::INVALIDATION_PROPERTIES_CHANGED |
                ViewState::INVALIDATION_PARENT_CHANGED))) {
    mozart::ViewPropertiesPtr properties = ResolveViewProperties(view_state);
    if (properties) {
      if (!view_state->issued_properties() ||
          !properties->Equals(*view_state->issued_properties())) {
        view_state->IssueProperties(properties.Pass());
        PublishStubScene(view_state);
        view_properties_changed = true;
      }
    }
  }

  // If we don't have view properties yet then we cannot pursue traversals.
  // Remember the application-specified invalidation bits for later.
  if (!view_state->issued_properties()) {
    DVLOG(2) << "View has no valid properties: view=" << view_state;
    view_state->set_invalidation_flags(flags &
                                       ViewState::INVALIDATION_EXPLICIT);
    return;
  }

  view_state->set_invalidation_flags(0u);

  // Deliver invalidation event if needed.
  if (view_properties_changed || (flags & ViewState::INVALIDATION_EXPLICIT)) {
    auto invalidation = mozart::ViewInvalidation::New();
    if (view_properties_changed)
      invalidation->properties = view_state->issued_properties().Clone();
    invalidation->scene_version = view_state->issued_scene_version();
    invalidation->frame_info = frame_info->Clone();
    SendInvalidation(view_state, invalidation.Pass());
  }

  // TODO(jeffbrown): Optimize propagation.
  // This should defer traversal of the rest of the subtree until the view
  // flushes its container or a timeout expires.  We will need to be careful
  // to ensure that we completely process one traversal before starting the
  // next one and we'll have to retain some state.  The same behavior should
  // be applied when the parent's own properties change (assuming that it is
  // likely to want to resize its children, unless it says otherwise somehow).

  // Traverse all children.
  for (const auto& pair : view_state->children()) {
    if (pair.second->state())
      TraverseView(pair.second->state(), frame_info, view_properties_changed);
  }
}

mozart::ViewPropertiesPtr ViewRegistry::ResolveViewProperties(
    ViewState* view_state) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));

  ViewStub* view_stub = view_state->view_stub();
  if (!view_stub || !view_stub->properties())
    return nullptr;

  if (view_stub->parent()) {
    if (!view_stub->parent()->issued_properties())
      return nullptr;
    mozart::ViewPropertiesPtr properties =
        view_stub->parent()->issued_properties().Clone();
    ApplyOverrides(properties.get(), view_stub->properties().get());
    return properties.Pass();
  } else if (view_stub->is_root_of_tree()) {
    if (!view_stub->properties() || !IsComplete(*view_stub->properties())) {
      DVLOG(2) << "View tree properties are incomplete: root=" << view_state
               << ", properties=" << view_stub->properties();
      return nullptr;
    }
    return view_stub->properties().Clone();
  } else {
    return nullptr;
  }
}

// VIEW AND VIEW TREE SERVICE PROVIDERS

void ViewRegistry::ConnectToViewService(
    ViewState* view_state,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  FTL_DCHECK(IsViewStateRegisteredDebug(view_state));

  associate_table_.ConnectToViewService(view_state->view_token()->Clone(),
                                        service_name, client_handle.Pass());
}

void ViewRegistry::ConnectToViewTreeService(
    ViewTreeState* tree_state,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));

  associate_table_.ConnectToViewTreeService(
      tree_state->view_tree_token()->Clone(), service_name,
      client_handle.Pass());
}

// VIEW INSPECTOR

void ViewRegistry::GetHitTester(
    mozart::ViewTreeTokenPtr view_tree_token,
    mojo::InterfaceRequest<mozart::HitTester> hit_tester_request,
    const GetHitTesterCallback& callback) {
  FTL_DCHECK(view_tree_token);
  FTL_DCHECK(hit_tester_request.is_pending());
  DVLOG(1) << "GetHitTester: tree=" << view_tree_token;

  ViewTreeState* view_tree = FindViewTree(view_tree_token->value);
  if (!view_tree) {
    callback.Run(false);
    return;
  }

  view_tree->RequestHitTester(hit_tester_request.Pass(), callback);
}

void ViewRegistry::ResolveScenes(
    mojo::Array<mozart::SceneTokenPtr> scene_tokens,
    const ResolveScenesCallback& callback) {
  FTL_DCHECK(scene_tokens);

  mojo::Array<mozart::ViewTokenPtr> result;
  result.resize(scene_tokens.size());

  size_t index = 0;
  for (const auto& scene_token : scene_tokens.storage()) {
    FTL_DCHECK(scene_token);
    auto it = views_by_scene_token_.find(scene_token->value);
    if (it != views_by_scene_token_.end())
      result[index] = it->second->view_token()->Clone();
    index++;
  }

  DVLOG(1) << "ResolveScenes: scene_tokens=" << scene_tokens
           << ", result=" << result;
  callback.Run(result.Pass());
}

// EXTERNAL SIGNALING

void ViewRegistry::SendInvalidation(ViewState* view_state,
                                    mozart::ViewInvalidationPtr invalidation) {
  FTL_DCHECK(view_state);
  FTL_DCHECK(invalidation);
  FTL_DCHECK(view_state->view_listener());

  // TODO: Detect ANRs
  DVLOG(1) << "SendInvalidation: view_state=" << view_state
           << ", invalidation=" << invalidation;
  view_state->view_listener()->OnInvalidation(invalidation.Pass(), [] {});
}

void ViewRegistry::SendRendererDied(ViewTreeState* tree_state) {
  FTL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  FTL_DCHECK(tree_state->view_tree_listener());

  // TODO: Detect ANRs
  DVLOG(1) << "SendRendererDied: tree_state=" << tree_state;
  tree_state->view_tree_listener()->OnRendererDied([] {});
}

void ViewRegistry::SendChildAttached(ViewContainerState* container_state,
                                     uint32_t child_key,
                                     mozart::ViewInfoPtr child_view_info) {
  FTL_DCHECK(container_state);
  FTL_DCHECK(child_view_info);

  if (!container_state->view_container_listener())
    return;

  // TODO: Detect ANRs
  DVLOG(1) << "SendChildAttached: container_state=" << container_state
           << ", child_key=" << child_key
           << ", child_view_info=" << child_view_info;
  container_state->view_container_listener()->OnChildAttached(
      child_key, child_view_info.Pass(), [] {});
}

void ViewRegistry::SendChildUnavailable(ViewContainerState* container_state,
                                        uint32_t child_key) {
  FTL_DCHECK(container_state);

  if (!container_state->view_container_listener())
    return;

  // TODO: Detect ANRs
  DVLOG(1) << "SendChildUnavailable: container=" << container_state
           << ", child_key=" << child_key;
  container_state->view_container_listener()->OnChildUnavailable(child_key,
                                                                 [] {});
}

// LOOKUP

ViewState* ViewRegistry::FindView(uint32_t view_token_value) {
  auto it = views_by_token_.find(view_token_value);
  return it != views_by_token_.end() ? it->second : nullptr;
}

ViewTreeState* ViewRegistry::FindViewTree(uint32_t view_tree_token_value) {
  auto it = view_trees_by_token_.find(view_tree_token_value);
  return it != view_trees_by_token_.end() ? it->second : nullptr;
}

}  // namespace view_manager
