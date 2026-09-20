// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"

namespace UVE::Asset {

/// CPU linear-blend skinning: the authority a GPU skinning path will be verified against.
///
/// This exists before any GPU kernel on purpose. CS4, CS5 and CS8 were all verifiable because a
/// CPU implementation of the same maths already existed to compare against - skinning had no such
/// baseline, which is exactly why the ROADMAP listed it as blocked. A GPU skinning pass written
/// first would have nothing to be right or wrong against.
///
/// Linear blend skinning, plainly: each vertex is transformed by every joint that influences it
/// and the results are mixed by weight. The well-known artifact - volume collapsing at strongly
/// twisted joints, the "candy wrapper" - is inherent to the method, not a defect here; dual
/// quaternion skinning is the alternative and is a different feature, not a bug fix.
///
/// Thread-safety: free functions over caller-owned data, no shared state.

/// Resolves `localPose` (one local-space transform per joint, in MeshAssetUVE::joints order) into
/// the model-space SKINNING matrices a vertex transform consumes - that is, each joint's resolved
/// world transform composed with its inverse bind matrix.
///
/// One forward pass, no recursion: the skeleton's topological ordering guarantees every parent is
/// already resolved when its child is reached. Returns false, leaving `outSkinningMatrices`
/// untouched, if the pose size does not match the joint count or the skeleton is not ordered.
[[nodiscard]] bool TryResolvePoseUVE(std::span<const MeshJointUVE> joints,
                                     std::span<const Math::Matrix4x4UVE> localPose,
                                     std::vector<Math::Matrix4x4UVE>& outSkinningMatrices);

/// Skins `mesh` with `skinningMatrices` (as produced by TryResolvePoseUVE) into `outVertices`.
///
/// Positions are transformed as points and normals/tangents as directions - the distinction
/// matters: a skinning matrix has a translation, and applying it to a normal would move the
/// direction by the joint's position instead of merely reorienting it.
///
/// Normals and tangents are transformed by the same blended matrix as the position rather than by
/// its inverse transpose. That is a deliberate, documented approximation: it is exact for the
/// rigid and uniformly-scaled joints that skeletal animation overwhelmingly uses, it is what every
/// mainstream real-time GPU skinning path does, and computing a per-vertex inverse transpose would
/// make the CPU baseline unable to match a GPU implementation that does not. Non-uniformly scaled
/// joints will shear normals; that is the known cost.
///
/// Returns false, leaving `outVertices` untouched, if the mesh is not skinned, its skinning data
/// is structurally invalid, the matrix count does not match the joint count, or any result is
/// non-finite. `outVertices` is sized to match `mesh.vertices`.
[[nodiscard]] bool TrySkinMeshUVE(const MeshAssetUVE& mesh,
                                  std::span<const Math::Matrix4x4UVE> skinningMatrices,
                                  std::vector<MeshVertexUVE>& outVertices);

} // namespace UVE::Asset
