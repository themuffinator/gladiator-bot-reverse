#pragma once

#include <cstddef>
#include <cstdint>

#include "legacy_common.hpp"

namespace bspc::aas
{

struct TmpArea;

struct TmpFace
{
    int num = 0;
    int planenum = 0;
    legacy::WindingPtr winding{};
    TmpArea *frontarea = nullptr;
    TmpArea *backarea = nullptr;
    int faceflags = 0;
    int aasfacenum = 0;
    TmpFace *prev[2] = {nullptr, nullptr};
    TmpFace *next[2] = {nullptr, nullptr};
    TmpFace *l_prev = nullptr;
    TmpFace *l_next = nullptr;
};

struct TmpAreaSettings
{
    int contents = 0;
    int modelnum = 0;
    int areaflags = 0;
    int presencetype = 0;
    int numreachableareas = 0;
    int firstreachablearea = 0;
};

struct TmpArea
{
    int areanum = 0;
    TmpFace *tmpfaces = nullptr;
    int presencetype = 0;
    int contents = 0;
    int modelnum = 0;
    bool invalid = false;
    TmpAreaSettings *settings = nullptr;
    TmpArea *mergedarea = nullptr;
    int aasareanum = 0;
    TmpArea *l_prev = nullptr;
    TmpArea *l_next = nullptr;
};

struct TmpNode
{
    int planenum = 0;
    TmpArea *tmparea = nullptr;
    TmpNode *children[2] = {nullptr, nullptr};
};

struct TmpAasWorld
{
    int numfaces = 0;
    int facenum = 0;
    TmpFace *faces = nullptr;

    int numareas = 0;
    int areanum = 0;
    TmpArea *areas = nullptr;

    int numareasettings = 0;
    TmpAreaSettings *areasettings = nullptr;

    int numnodes = 0;
    TmpNode *nodes = nullptr;
};

TmpAasWorld &WorldState() noexcept;
void ResetWorldState();

TmpFace *AllocTmpFace();
void FreeTmpFace(TmpFace *face) noexcept;

TmpArea *AllocTmpArea();
void FreeTmpArea(TmpArea *area) noexcept;

TmpAreaSettings *AllocTmpAreaSettings();

TmpNode *AllocTmpNode();

} // namespace bspc::aas

