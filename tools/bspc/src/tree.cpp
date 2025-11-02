#include "tree.hpp"

#include <utility>

#include "portals.hpp"

namespace bspc::legacy
{
namespace
{

void RemovePortalChain(Node &node)
{
    for (Portal *portal = node.portals; portal; )
    {
        Portal *next = portal->next[(portal->nodes[1] == &node) ? 1 : 0];
        RemovePortalFromNode(*portal, node);
        FreePortal(portal);
        portal = next;
    }
    node.portals = nullptr;
}

} // namespace

std::unique_ptr<Tree> Tree_Alloc()
{
    auto tree = std::make_unique<Tree>();
    tree->headnode = std::make_unique<Node>();
    tree->headnode->planenum = kPlanenumLeaf;
    tree->outside_node.planenum = kPlanenumLeaf;
    return tree;
}

Node *NodeForPoint(Node &node, const Vec3 &origin)
{
    Node *current = &node;
    while (current->planenum != kPlanenumLeaf)
    {
        const Plane &plane = MapPlanes().at(static_cast<std::size_t>(current->planenum));
        const double distance = DistanceToPlane(plane, origin);
        const int child_index = (distance >= 0.0) ? 0 : 1;
        if (!current->children[child_index])
        {
            break;
        }
        current = current->children[child_index];
    }
    return current;
}

void Tree_FreePortals_r(Node &node)
{
    if (node.children[0])
    {
        Tree_FreePortals_r(*node.children[0]);
    }
    if (node.children[1])
    {
        Tree_FreePortals_r(*node.children[1]);
    }

    RemovePortalChain(node);
}

void Tree_Free_r(Node &node)
{
    if (node.children[0])
    {
        Tree_Free_r(*node.children[0]);
        node.owned_children[0].reset();
        node.children[0] = nullptr;
    }
    if (node.children[1])
    {
        Tree_Free_r(*node.children[1]);
        node.owned_children[1].reset();
        node.children[1] = nullptr;
    }

    node.brushlist = nullptr;
    node.volume.reset();
}

void Tree_Free(Tree &tree)
{
    if (tree.headnode)
    {
        Tree_FreePortals_r(*tree.headnode);
        Tree_Free_r(*tree.headnode);
        tree.headnode.reset();
    }

    RemovePortalChain(tree.outside_node);
    tree.extra_nodes.clear();
    tree.owned_entities.clear();
}

void Tree_Free(std::unique_ptr<Tree> &tree)
{
    if (tree)
    {
        Tree_Free(*tree);
        tree.reset();
    }
}

Node &Tree_EmplaceLeaf(Tree &tree)
{
    auto node = std::make_unique<Node>();
    node->planenum = kPlanenumLeaf;
    Node &ref = *node;
    tree.extra_nodes.push_back(std::move(node));
    return ref;
}

Entity &Tree_EmplaceEntity(Tree &tree, const Vec3 &origin, std::string classname)
{
    auto entity = std::make_unique<Entity>();
    entity->classname = std::move(classname);
    entity->origin = origin;
    Entity &ref = *entity;
    tree.owned_entities.push_back(std::move(entity));
    return ref;
}

} // namespace bspc::legacy

