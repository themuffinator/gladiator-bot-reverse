#pragma once

#include <memory>
#include <string>

#include "legacy_common.hpp"

namespace bspc::legacy
{

std::unique_ptr<Tree> Tree_Alloc();
Node *NodeForPoint(Node &node, const Vec3 &origin);
void Tree_FreePortals_r(Node &node);
void Tree_Free_r(Node &node);
void Tree_Free(Tree &tree);
void Tree_Free(std::unique_ptr<Tree> &tree);
Node &Tree_EmplaceLeaf(Tree &tree);
Entity &Tree_EmplaceEntity(Tree &tree, const Vec3 &origin, std::string classname = "worldspawn");

} // namespace bspc::legacy

