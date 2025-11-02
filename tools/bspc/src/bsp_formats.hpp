#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "memory.h"

namespace bspc::formats
{

struct LumpRange
{
    std::int32_t offset = 0;
    std::int32_t length = 0;

    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return length <= 0;
    }

    [[nodiscard]] bool FitsIn(std::size_t file_size) const noexcept
    {
        if (offset < 0 || length < 0)
        {
            return false;
        }
        const auto uoffset = static_cast<std::size_t>(offset);
        const auto ulength = static_cast<std::size_t>(length);
        return uoffset <= file_size && ulength <= file_size - uoffset;
    }
};

static_assert(sizeof(LumpRange) == 8, "Unexpected padding in LumpRange");

struct ConstByteSpan
{
    const std::byte *data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return size == 0;
    }
};

struct MutableByteSpan
{
    std::byte *data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return size == 0;
    }
};

struct LumpView
{
    const std::byte *data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return size == 0;
    }
};

struct MutableLumpView
{
    std::byte *data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return size == 0;
    }
};

struct MemoryDeleter
{
    void operator()(void *ptr) const noexcept
    {
        if (ptr)
        {
            memory::FreeMemory(ptr);
        }
    }
};

using UniqueMemory = std::unique_ptr<void, MemoryDeleter>;

struct OwnedLump
{
    UniqueMemory data;
    std::size_t size = 0;

    void Reset() noexcept
    {
        data.reset();
        size = 0;
    }

    void Allocate(std::size_t size_bytes, bool clear = true)
    {
        Reset();
        if (size_bytes == 0)
        {
            return;
        }

        void *memory = clear ? memory::GetClearedMemory(size_bytes) : memory::GetMemory(size_bytes);
        data.reset(memory);
        size = size_bytes;
    }

    void CopyFrom(LumpView source, bool clear = false)
    {
        if (source.size > 0 && source.data == nullptr)
        {
            Reset();
            return;
        }
        Allocate(source.size, clear);
        if (source.size > 0)
        {
            std::memcpy(data.get(), source.data, source.size);
        }
    }

    template <typename T>
    [[nodiscard]] std::pair<T *, std::size_t> AsSpan() noexcept
    {
        if (size % sizeof(T) != 0)
        {
            return {nullptr, 0};
        }
        return {static_cast<T *>(data.get()), size / sizeof(T)};
    }

    template <typename T>
    [[nodiscard]] std::pair<const T *, std::size_t> AsSpan() const noexcept
    {
        if (size % sizeof(T) != 0)
        {
            return {nullptr, 0};
        }
        return {static_cast<const T *>(data.get()), size / sizeof(T)};
    }
};

constexpr std::size_t kHalfLifeLumpCount = 15;
constexpr std::int32_t kHalfLifeBspVersion = 30;

enum class HalfLifeLump : std::size_t
{
    kEntities = 0,
    kPlanes,
    kTextures,
    kVertices,
    kVisibility,
    kNodes,
    kTexInfo,
    kFaces,
    kLighting,
    kClipNodes,
    kLeaves,
    kMarkSurfaces,
    kEdges,
    kSurfEdges,
    kModels,
};

struct HalfLifeBspHeader
{
    std::int32_t version = 0;
    std::array<LumpRange, kHalfLifeLumpCount> lumps{};
};

static_assert(sizeof(HalfLifeBspHeader) == 4 + kHalfLifeLumpCount * sizeof(LumpRange),
              "Half-Life BSP header layout must match file format");

struct HalfLifeBspView
{
    HalfLifeBspHeader header{};
    std::array<LumpView, kHalfLifeLumpCount> lumps{};
};

constexpr std::size_t kQuake1LumpCount = 15;
constexpr std::int32_t kQuake1BspVersion = 29;

enum class Quake1Lump : std::size_t
{
    kEntities = 0,
    kPlanes,
    kTextures,
    kVertices,
    kVisibility,
    kNodes,
    kTexInfo,
    kFaces,
    kLighting,
    kClipNodes,
    kLeaves,
    kMarkSurfaces,
    kEdges,
    kSurfEdges,
    kModels,
};

struct Quake1BspHeader
{
    std::int32_t version = 0;
    std::array<LumpRange, kQuake1LumpCount> lumps{};
};

static_assert(sizeof(Quake1BspHeader) == 4 + kQuake1LumpCount * sizeof(LumpRange),
              "Quake 1 BSP header layout must match file format");

struct Quake1BspView
{
    Quake1BspHeader header{};
    std::array<LumpView, kQuake1LumpCount> lumps{};
};

constexpr std::size_t kQuake2LumpCount = 19;
constexpr std::int32_t kQuake2BspIdent = ('P' << 24) | ('S' << 16) | ('B' << 8) | 'I';
constexpr std::int32_t kQuake2BspVersion = 38;

enum class Quake2Lump : std::size_t
{
    kEntities = 0,
    kPlanes,
    kVertices,
    kVisibility,
    kNodes,
    kTexInfo,
    kFaces,
    kLighting,
    kLeaves,
    kLeafFaces,
    kLeafBrushes,
    kEdges,
    kSurfEdges,
    kModels,
    kBrushes,
    kBrushSides,
    kPop,
    kAreas,
    kAreaPortals,
};

struct Quake2BspHeader
{
    std::int32_t ident = 0;
    std::int32_t version = 0;
    std::array<LumpRange, kQuake2LumpCount> lumps{};
};

static_assert(sizeof(Quake2BspHeader) == 8 + kQuake2LumpCount * sizeof(LumpRange),
              "Quake 2 BSP header layout must match file format");

struct Quake2BspView
{
    Quake2BspHeader header{};
    std::array<LumpView, kQuake2LumpCount> lumps{};
};

struct Quake2Plane
{
    float normal[3] = {0.f, 0.f, 0.f};
    float dist = 0.f;
    std::int32_t type = 0;
};
static_assert(sizeof(Quake2Plane) == 20, "Quake 2 plane layout must match legacy format");

struct Quake2Node
{
    std::int32_t planenum = 0;
    std::int32_t children[2] = {0, 0};
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_face = 0;
    std::uint16_t face_count = 0;
};
static_assert(sizeof(Quake2Node) == 28, "Quake 2 node layout must match legacy format");

struct Quake2Leaf
{
    std::int32_t contents = 0;
    std::int16_t cluster = 0;
    std::int16_t area = 0;
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_leaf_face = 0;
    std::uint16_t leaf_face_count = 0;
    std::uint16_t first_leaf_brush = 0;
    std::uint16_t leaf_brush_count = 0;
};
static_assert(sizeof(Quake2Leaf) == 28, "Quake 2 leaf layout must match legacy format");

struct Quake2Face
{
    std::uint16_t plane_index = 0;
    std::int16_t plane_side = 0;
    std::int32_t first_edge = 0;
    std::int16_t edge_count = 0;
    std::int16_t texinfo = 0;
    std::uint8_t styles[4] = {0, 0, 0, 0};
    std::int32_t lightmap_offset = 0;
};
static_assert(sizeof(Quake2Face) == 20, "Quake 2 face layout must match legacy format");

struct Quake2TexInfo
{
    float vecs[2][4] = {{0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}};
    std::int32_t flags = 0;
    std::int32_t value = 0;
    char texture[32] = {0};
    std::int32_t next_texinfo = -1;
};
static_assert(sizeof(Quake2TexInfo) == 76, "Quake 2 texinfo layout must match legacy format");

struct Quake2Model
{
    float mins[3] = {0.f, 0.f, 0.f};
    float maxs[3] = {0.f, 0.f, 0.f};
    float origin[3] = {0.f, 0.f, 0.f};
    std::int32_t headnode = -1;
    std::int32_t first_face = 0;
    std::int32_t face_count = 0;
};
static_assert(sizeof(Quake2Model) == 48, "Quake 2 model layout must match legacy format");

struct Quake2Brush
{
    std::int32_t first_side = 0;
    std::int32_t side_count = 0;
    std::int32_t contents = 0;
};
static_assert(sizeof(Quake2Brush) == 12, "Quake 2 brush layout must match legacy format");

struct Quake2BrushSide
{
    std::uint16_t plane_index = 0;
    std::int16_t texinfo = 0;
};
static_assert(sizeof(Quake2BrushSide) == 4, "Quake 2 brush side layout must match legacy format");

struct Quake2Area
{
    std::int32_t portal_count = 0;
    std::int32_t first_portal = 0;
};
static_assert(sizeof(Quake2Area) == 8, "Quake 2 area layout must match legacy format");

struct Quake2AreaPortal
{
    std::int32_t portal_index = 0;
    std::int32_t other_area = 0;
};
static_assert(sizeof(Quake2AreaPortal) == 8, "Quake 2 area portal layout must match legacy format");

struct Quake2Edge
{
    std::uint16_t vertices[2] = {0, 0};
};
static_assert(sizeof(Quake2Edge) == 4, "Quake 2 edge layout must match legacy format");

struct Quake2Vertex
{
    float position[3] = {0.f, 0.f, 0.f};
};
static_assert(sizeof(Quake2Vertex) == 12, "Quake 2 vertex layout must match legacy format");

constexpr std::size_t kSinLumpCount = 20;
constexpr std::int32_t kSinBspIdent = kQuake2BspIdent;
constexpr std::int32_t kSinBspVersion = 41;

enum class SinLump : std::size_t
{
    kEntities = 0,
    kPlanes,
    kVertices,
    kVisibility,
    kNodes,
    kTexInfo,
    kFaces,
    kLighting,
    kLeaves,
    kLeafFaces,
    kLeafBrushes,
    kEdges,
    kSurfEdges,
    kModels,
    kBrushes,
    kBrushSides,
    kPop,
    kAreas,
    kAreaPortals,
    kLightInfo,
};

struct SinBspHeader
{
    std::int32_t ident = 0;
    std::int32_t version = 0;
    std::array<LumpRange, kSinLumpCount> lumps{};
};

static_assert(sizeof(SinBspHeader) == 8 + kSinLumpCount * sizeof(LumpRange),
              "SiN BSP header layout must match file format");

struct SinBspView
{
    SinBspHeader header{};
    std::array<LumpView, kSinLumpCount> lumps{};
};

constexpr std::size_t kAasLumpCount = 14;
constexpr std::int32_t kAasIdent = ('S' << 24) | ('A' << 16) | ('A' << 8) | 'E';
constexpr std::int32_t kAasVersion = 5;

enum class AasLump : std::size_t
{
    kBBoxes = 0,
    kVertices,
    kPlanes,
    kEdges,
    kEdgeIndex,
    kFaces,
    kFaceIndex,
    kAreas,
    kAreaSettings,
    kReachability,
    kNodes,
    kPortals,
    kPortalIndex,
    kClusters,
};

struct AasHeader
{
    std::int32_t ident = 0;
    std::int32_t version = 0;
    std::int32_t bsp_checksum = 0;
    std::array<LumpRange, kAasLumpCount> lumps{};
};

static_assert(sizeof(AasHeader) == 12 + kAasLumpCount * sizeof(LumpRange),
              "AAS header layout must match file format");

struct AasView
{
    AasHeader header{};
    std::array<LumpView, kAasLumpCount> lumps{};
};

struct AasBBox
{
    std::int32_t presence_type = 0;
    std::int32_t flags = 0;
    float mins[3] = {0.f, 0.f, 0.f};
    float maxs[3] = {0.f, 0.f, 0.f};
};
static_assert(sizeof(AasBBox) == 32, "AAS bbox layout must match legacy format");

struct AasReachability
{
    std::int32_t area = 0;
    std::int32_t face = 0;
    std::int32_t edge = 0;
    float start[3] = {0.f, 0.f, 0.f};
    float end[3] = {0.f, 0.f, 0.f};
    std::int32_t travel_type = 0;
    std::uint16_t travel_time = 0;
    std::uint16_t padding = 0;
};
static_assert(sizeof(AasReachability) == 44, "AAS reachability layout must match legacy format");

struct AasAreaSettings
{
    std::int32_t contents = 0;
    std::int32_t area_flags = 0;
    std::int32_t presence_type = 0;
    std::int32_t cluster = 0;
    std::int32_t cluster_area_num = 0;
    std::int32_t num_reachable_areas = 0;
    std::int32_t first_reachable_area = 0;
};
static_assert(sizeof(AasAreaSettings) == 28, "AAS area settings layout must match legacy format");

struct AasPortal
{
    std::int32_t area = 0;
    std::int32_t front_cluster = 0;
    std::int32_t back_cluster = 0;
    std::int32_t cluster_area_num[2] = {0, 0};
};
static_assert(sizeof(AasPortal) == 20, "AAS portal layout must match legacy format");

struct AasCluster
{
    std::int32_t num_areas = 0;
    std::int32_t num_reachability_areas = 0;
    std::int32_t num_portals = 0;
    std::int32_t first_portal = 0;
};
static_assert(sizeof(AasCluster) == 16, "AAS cluster layout must match legacy format");

struct AasPlane
{
    float normal[3] = {0.f, 0.f, 0.f};
    float dist = 0.f;
    std::int32_t type = 0;
};
static_assert(sizeof(AasPlane) == 20, "AAS plane layout must match legacy format");

struct AasEdge
{
    std::int32_t vertices[2] = {0, 0};
};
static_assert(sizeof(AasEdge) == 8, "AAS edge layout must match legacy format");

struct AasFace
{
    std::int32_t plane = 0;
    std::int32_t flags = 0;
    std::int32_t num_edges = 0;
    std::int32_t first_edge = 0;
    std::int32_t front_area = 0;
    std::int32_t back_area = 0;
};
static_assert(sizeof(AasFace) == 24, "AAS face layout must match legacy format");

struct AasArea
{
    std::int32_t index = 0;
    std::int32_t num_faces = 0;
    std::int32_t first_face = 0;
    float mins[3] = {0.f, 0.f, 0.f};
    float maxs[3] = {0.f, 0.f, 0.f};
    float center[3] = {0.f, 0.f, 0.f};
};
static_assert(sizeof(AasArea) == 48, "AAS area layout must match legacy format");

struct AasNode
{
    std::int32_t plane = 0;
    std::int32_t children[2] = {0, 0};
};
static_assert(sizeof(AasNode) == 12, "AAS node layout must match legacy format");

bool DeserializeHalfLifeBsp(ConstByteSpan file_data, HalfLifeBspView &out, std::string &error);

bool DeserializeQuake1Bsp(ConstByteSpan file_data, Quake1BspView &out, std::string &error);

bool DeserializeQuake2Bsp(ConstByteSpan file_data, Quake2BspView &out, std::string &error);

bool DeserializeAas(ConstByteSpan file_data, AasView &out, std::string &error);

bool SerializeHalfLifeBsp(const std::array<LumpView, kHalfLifeLumpCount> &lumps,
                          std::vector<std::byte> &output,
                          std::string &error);

bool SerializeQuake1Bsp(const std::array<LumpView, kQuake1LumpCount> &lumps,
                        std::vector<std::byte> &output,
                        std::string &error);

bool SerializeQuake2Bsp(const std::array<LumpView, kQuake2LumpCount> &lumps,
                        std::vector<std::byte> &output,
                        std::string &error);

bool SerializeAas(const AasHeader &header_template,
                  const std::array<LumpView, kAasLumpCount> &lumps,
                  std::vector<std::byte> &output,
                  std::string &error);

} // namespace bspc::formats

