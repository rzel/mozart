// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_

#include "apps/mozart/services/input/interfaces/input_connection.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace input_manager {

class InputAssociate;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public mozart::InputConnection {
 public:
  InputConnectionImpl(InputAssociate* associate,
                      mozart::ViewTokenPtr view_token,
                      mojo::InterfaceRequest<mozart::InputConnection> request);
  ~InputConnectionImpl() override;

  const mozart::ViewToken* view_token() const { return view_token_.get(); }

  // Delivers an event to a view.
  void DeliverEvent(mozart::EventPtr event);

  // |mozart::InputConnection|
  void SetListener(
      mojo::InterfaceHandle<mozart::InputListener> listener) override;

 private:
  void OnEventFinished(bool handled);

  InputAssociate* const associate_;
  mozart::ViewTokenPtr view_token_;
  mozart::InputListenerPtr listener_;

  mojo::Binding<mozart::InputConnection> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace input_manager

#endif  // APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
