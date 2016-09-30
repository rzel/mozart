// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_ASSOCIATES_TEST_HELPERS_H_
#define MOJO_UI_ASSOCIATES_TEST_HELPERS_H_

#include <utility>

#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "lib/ftl/functional/closure.h"

namespace test {

template <typename T>
void Capture(const ftl::Closure& quit, T* out, T value) {
  *out = std::move(value);
  quit();
}

inline mojo::PointFPtr MakePointF(float x, float y) {
  auto result = mojo::PointF::New();
  result->x = x;
  result->y = y;
  return result.Pass();
}

inline mojo::TransformPtr MakeDummyTransform(float x) {
  auto result = mojo::Transform::New();
  result->matrix.resize(16u);
  result->matrix[0] = x;
  return result.Pass();
}

inline mozart::SceneTokenPtr MakeDummySceneToken(uint32_t value) {
  auto result = mozart::SceneToken::New();
  result->value = value;
  return result.Pass();
}

inline mozart::ViewTokenPtr MakeDummyViewToken(uint32_t value) {
  auto result = mozart::ViewToken::New();
  result->value = value;
  return result.Pass();
}

inline mozart::HitTestResultPtr MakeSimpleHitTestResult(
    mozart::SceneTokenPtr scene_token,
    mojo::TransformPtr transform) {
  auto result = mozart::HitTestResult::New();
  result->root = mozart::SceneHit::New();
  result->root->scene_token = scene_token.Pass();
  result->root->hits.push_back(mozart::Hit::New());
  result->root->hits[0]->set_node(mozart::NodeHit::New());
  result->root->hits[0]->get_node()->transform = transform.Pass();
  return result.Pass();
}

inline mozart::HitTestResultPtr MakeSimpleHitTestResult(
    mozart::SceneTokenPtr scene_token) {
  return MakeSimpleHitTestResult(scene_token.Pass(), MakeDummyTransform(0.f));
}

}  // namespace test

#endif  // MOJO_UI_ASSOCIATES_TEST_HELPERS_H_
