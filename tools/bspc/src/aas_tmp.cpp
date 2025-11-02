#include "aas_tmp.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include "memory.h"

namespace bspc::aas
{
namespace
{
struct ArenaView
{
    std::byte *data = nullptr;
    std::size_t size_bytes = 0;
    std::size_t used_bytes = 0;
};

struct TmpAasArenaViews
{
    ArenaView faces;
    ArenaView areas;
    ArenaView area_settings;
    ArenaView nodes;
};

TmpAasWorld g_world_state;
TmpAasArenaViews g_views;

std::byte *AsBytePtr(void *ptr) noexcept
{
    return static_cast<std::byte *>(ptr);
}

ArenaView MakeView(memory::ArenaBuffer &buffer)
{
    ArenaView view{};
    view.data = AsBytePtr(buffer.data);
    view.size_bytes = buffer.size_bytes;
    view.used_bytes = buffer.used_count;
    return view;
}

void SyncUsage(memory::ArenaBuffer &buffer, const ArenaView &view)
{
    buffer.used_count = view.used_bytes;
}

std::size_t AlignOffset(std::size_t offset, std::size_t alignment)
{
    const std::size_t mask = alignment - 1;
    return (offset + mask) & ~mask;
}

template <typename T>
T *AllocFromView(ArenaView &view)
{
    constexpr std::size_t alignment = alignof(T);
    std::size_t offset = AlignOffset(view.used_bytes, alignment);
    const std::size_t end = offset + sizeof(T);
    if (end > view.size_bytes)
    {
        throw std::runtime_error("tmp AAS arena exhausted");
    }

    T *result = reinterpret_cast<T *>(view.data + offset);
    std::memset(result, 0, sizeof(T));
    view.used_bytes = end;
    return result;
}

void SyncViews()
{
    auto &arena = memory::TmpAasArenaState();
    SyncUsage(arena.faces, g_views.faces);
    SyncUsage(arena.areas, g_views.areas);
    SyncUsage(arena.area_settings, g_views.area_settings);
    SyncUsage(arena.nodes, g_views.nodes);
}

void InitialiseViews()
{
    memory::InitTmpAasArena();
    auto &arena = memory::TmpAasArenaState();
    g_views.faces = MakeView(arena.faces);
    g_views.areas = MakeView(arena.areas);
    g_views.area_settings = MakeView(arena.area_settings);
    g_views.nodes = MakeView(arena.nodes);
}

} // namespace

TmpAasWorld &WorldState() noexcept
{
    return g_world_state;
}

void ResetWorldState()
{
    InitialiseViews();

    g_world_state = {};

    auto &arena = memory::TmpAasArenaState();
    arena.faces.used_count = 0;
    arena.areas.used_count = 0;
    arena.area_settings.used_count = 0;
    arena.nodes.used_count = 0;

    g_views = {};
    g_views.faces = MakeView(arena.faces);
    g_views.areas = MakeView(arena.areas);
    g_views.area_settings = MakeView(arena.area_settings);
    g_views.nodes = MakeView(arena.nodes);
}

TmpFace *AllocTmpFace()
{
    TmpFace *face = AllocFromView<TmpFace>(g_views.faces);
    face->num = g_world_state.facenum++;
    face->l_next = g_world_state.faces;
    if (face->l_next != nullptr)
    {
        face->l_next->l_prev = face;
    }
    g_world_state.faces = face;
    g_world_state.numfaces++;
    SyncViews();
    return face;
}

void FreeTmpFace(TmpFace *face) noexcept
{
    if (face == nullptr)
    {
        return;
    }

    if (face->l_next != nullptr)
    {
        face->l_next->l_prev = face->l_prev;
    }
    if (face->l_prev != nullptr)
    {
        face->l_prev->l_next = face->l_next;
    }
    else if (g_world_state.faces == face)
    {
        g_world_state.faces = face->l_next;
    }

    g_world_state.numfaces = std::max(0, g_world_state.numfaces - 1);
}

TmpArea *AllocTmpArea()
{
    TmpArea *area = AllocFromView<TmpArea>(g_views.areas);
    area->areanum = g_world_state.areanum++;
    area->l_next = g_world_state.areas;
    if (area->l_next != nullptr)
    {
        area->l_next->l_prev = area;
    }
    g_world_state.areas = area;
    g_world_state.numareas++;
    SyncViews();
    return area;
}

void FreeTmpArea(TmpArea *area) noexcept
{
    if (area == nullptr)
    {
        return;
    }

    if (area->l_next != nullptr)
    {
        area->l_next->l_prev = area->l_prev;
    }
    if (area->l_prev != nullptr)
    {
        area->l_prev->l_next = area->l_next;
    }
    else if (g_world_state.areas == area)
    {
        g_world_state.areas = area->l_next;
    }

    g_world_state.numareas = std::max(0, g_world_state.numareas - 1);
}

TmpAreaSettings *AllocTmpAreaSettings()
{
    TmpAreaSettings *settings = AllocFromView<TmpAreaSettings>(g_views.area_settings);
    g_world_state.numareasettings++;
    SyncViews();
    return settings;
}

TmpNode *AllocTmpNode()
{
    TmpNode *node = AllocFromView<TmpNode>(g_views.nodes);
    if (g_world_state.nodes == nullptr)
    {
        g_world_state.nodes = node;
    }
    g_world_state.numnodes++;
    SyncViews();
    return node;
}

} // namespace bspc::aas

