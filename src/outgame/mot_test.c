#include "common.h"
#include "typedefs.h"
#include "enums.h"
#include "outgame/mot_test.h"

#include "main/glob.h"
#include "mikupan/mikupan_memory.h"
#include "os/eeiop/cdvd/eecdvd.h"
#include "outgame/outgame.h"

#include "sce/libvu0.h"

#include "graphics/graph2d/effect_sub.h"
#include "graphics/graph2d/g2d_main.h"
#include "graphics/graph2d/message.h"
#include "graphics/graph3d/gra3d.h"
#include "graphics/graph3d/object.h"
#include "graphics/graph3d/sgcam.h"
#include "graphics/graph3d/sgdma.h"
#include "graphics/graph3d/sglight.h"
#include "graphics/graph3d/sgsu.h"
#include "graphics/motion/accessory.h"
#include "graphics/motion/mdldat.h"
#include "graphics/motion/mdlwork.h"
#include "graphics/motion/mime.h"
#include "graphics/motion/motion.h"

#include <stdio.h>
#include <string.h>

#define PI 3.1415927f
#define DEG2RAD(x) ((float)(x) * PI / 180.0f)

#define MOT_TEST_ENTRY_NUM ((int)(sizeof(mot_test_entries) / sizeof(mot_test_entries[0])))
#define MOT_TEST_MENU_NUM 11

typedef enum {
    MOT_TEST_LOAD_MODEL,
    MOT_TEST_WAIT_MODEL,
    MOT_TEST_LOAD_ANIM,
    MOT_TEST_WAIT_ANIM,
    MOT_TEST_READY,
} MOT_TEST_LOAD_STEP;

typedef struct {
    u_short mdl_no;
    u_short anm_no;
    char *name;
} MOT_TEST_ENTRY;

typedef struct {
    u_char file_id[4];
    u_int map_flg;
    u_int bone_num;
    u_int trans_num;
    u_int frame_num;
    u_int interp_frame;
    u_int flg;
    u_int si_frame;
} MOT_TEST_MOT_HEADER;

typedef struct {
    ANI_CTRL ani;
    SgCAMERA camera;
    MOT_TEST_LOAD_STEP load_step;
    int load_id;
    int entry_no;
    int anim_no;
    int anim_num;
    int frame;
    int frame_num;
    int menu_csr;
    int play;
    int loop;
    int loaded;
    float rot_y;
    float height;
    float zoom;
    float light_power;
} MOT_TEST_WRK;

static MOT_TEST_WRK mot_test_wrk;

static const MOT_TEST_ENTRY mot_test_entries[] = {
    {M000_MIKU,      A000_MIKU,      "M000 MIKU"},
    {M001_MAFUYU,    A001_MAFUYU,    "M001 MAFUYU"},
    {M010_HENREI,    A010_HENREI,    "M010 HENREI"},
    {M011_JYOREI,    A011_JYOREI,    "M011 JYOREI"},
    {M012_SAKKAREI,  A012_SAKKAREI,  "M012 SAKKAREI"},
    {M013_TENAGA,    A013_TENAGA,    "M013 TENAGA"},
    {M014_KAMIONNA,  A014_KAMIONNA,  "M014 KAMIONNA"},
    {M015_KOMUSO,    A015_KOMUSO,    "M015 KOMUSO"},
    {M016_MINREI,    A016_MINREI,    "M016 MINREI"},
    {M018_MAYOIGO1,  A018_MAYOIGO1,  "M018 MAYOIGO1"},
    {M019_BOUREI1,   A019_BOUREI1,   "M019 BOUREI1"},
    {M020_SAKASA,    A020_SAKASA,    "M020 SAKASA"},
    {M021_KUBI,      A021_KUBI,      "M021 KUBI"},
    {M022_MAYOIGO2,  A022_MAYOIGO2,  "M022 MAYOIGO2"},
    {M023_MAYOIGO3,  A023_MAYOIGO3,  "M023 MAYOIGO3"},
    {M024_MEKAKUSHI, A024_MEKAKUSHI, "M024 MEKAKUSHI"},
    {M025_RTE,       A025_RTE,       "M025 RTE"},
    {M027_KONNA,     A027_KONNA,     "M027 KONNA"},
    {M028_KOTOKO,    A028_KOTOKO,    "M028 KOTOKO"},
    {M031_HAIREI,    A031_HAIREI,    "M031 HAIREI"},
    {M032_SHINKAN1,  A032_SHINKAN1,  "M032 SHINKAN1"},
    {M033_MINTSUMA,  A033_MINTSUMA,  "M033 MINTSUMA"},
    {M034_SHINKAN2,  A034_SHINKAN2,  "M034 SHINKAN2"},
    {M035_SHINKAN3,  A035_SHINKAN3,  "M035 SHINKAN3"},
    {M036_SHINKAN4,  A036_SHINKAN4,  "M036 SHINKAN4"},
    {M037_TOUSHU,    A037_TOUSHU,    "M037 TOUSHU"},
    {M038_NAWAMIKO,  A038_NAWAMIKO,  "M038 NAWAMIKO"},
    {M039_MAGONLY,   A039_DUMMY,     "M039 MAGONLY"},
    {M040_MAGATOKI,  A040_MAGATOKI,  "M040 MAGATOKI"},
    {M041_DUMMY,     A041_DUMMY,     "M041 DUMMY"},
    {M042_SYOUKI2,   A042_SYOUKIA,   "M042 SYOUKI2"},
    {M043_SYOUKI3,   A043_SYOUKIB,   "M043 SYOUKI3"},
    {M044_SYOUKI4,   A044_SYOUKIC,   "M044 SYOUKI4"},
    {M051_MKIRIE,    A051_MKIRIE,    "M051 MKIRIE"},
    {M058_BOUREI2,   A058_BOUREI2,   "M058 BOUREI2"},
    {M800_BOUREI1,   A800_BOUREI1,   "M800 BOUREI1"},
    {M801_SAKASA,    A801_SAKASA,    "M801 SAKASA"},
    {M802_KUBI,      A802_KUBI,      "M802 KUBI"},
    {M803_KONNA,     A803_KONNA,     "M803 KONNA"},
    {M804_KOTOKO,    A804_KOTOKO,    "M804 KOTOKO"},
    {M005_SAKKA,     A001_SAKKA,     "F001 SAKKA"},
    {M003_HENSYU,    A100_HENSYU,    "F100 HENSYU"},
    {M003_HENSYU,    A101_HENSYU,    "F101 HENSYU"},
    {M003_HENSYU,    A102_HENSYU,    "F102 HENSYU"},
    {M010_HENREI,    A103_HENREI,    "F103 HENREI"},
    {M010_HENREI,    A104_HENREI,    "F104 HENREI"},
    {M003_HENSYU,    A105_HENSYU,    "F105 HENSYU"},
    {M004_JYOSYU,    A106_JYOSYU,    "F106 JYOSYU"},
    {M024_MEKAKUSHI, A107_MEKAKUSHI, "F107 MEKAKUSHI"},
    {M004_JYOSYU,    A108_JYOSYU,    "F108 JYOSYU"},
    {M004_JYOSYU,    A109_JYOSYU,    "F109 JYOSYU"},
    {M033_MINTSUMA,  A110_MINTSUMA,  "F110 MINTSUMA"},
    {M021_KUBI,      A111_KUBI,      "F111 KUBI"},
    {M005_SAKKA,     A112_SAKKA,     "F112 SAKKA"},
    {M006_KOKIRIE,   A113_KOKIRIE,   "F113 KOKIRIE"},
    {M005_SAKKA,     A114_SAKKA,     "F114 SAKKA"},
    {M047_SAKKAN,    A115_SAKKAN,    "F115 SAKKAN"},
    {M001_MAFUYU,    A116_MAFUYU,    "F116 MAFUYU"},
    {M004_JYOSYU,    A119_JYOSYU,    "F119 JYOSYU"},
    {M006_KOKIRIE,   A120_KOKIRIE,   "F120 KOKIRIE"},
    {M004_JYOSYU,    A122_JYOSYU,    "F122 JYOSYU"},
    {M005_SAKKA,     A125_SAKKA,     "F125 SAKKA"},
    {M020_SAKASA,    A126_SAKASA,    "F126 SAKASA"},
    {M003_HENSYU,    A128_HENSYU,    "F128 HENSYU"},
    {M010_HENREI,    A129_HENREI,    "F129 HENREI"},
    {M009_HENSHITAI, A130_HENSHITAI, "F130 HENSHITAI"},
    {M029_MAYOIGO4,  A200_MAYOIGO4,  "F200 MAYOIGO4"},
    {M001_MAFUYU,    A205_MAFUYU,    "F205 MAFUYU"},
    {M018_MAYOIGO1,  A206_MAYOIGO1,  "F206 MAYOIGO1"},
    {M026_MINZOKU,   A207_MINZOKU,   "F207 MINZOKU"},
    {M006_KOKIRIE,   A209_KOKIRIE,   "F209 KOKIRIE"},
    {M026_MINZOKU,   A211_MINZOKU,   "F211 MINZOKU"},
    {M026_MINZOKU,   A213_MINZOKU,   "F213 MINZOKU"},
    {M026_MINZOKU,   A216_MINZOKU,   "F216 MINZOKU"},
    {M059_JITOUSHU,  A300_JITOUSHU,  "F300 JITOUSHU"},
    {M059_JITOUSHU,  A301_JITOUSHU,  "F301 JITOUSHU"},
    {M006_KOKIRIE,   A400_KOKIRIE,   "F400 KOKIRIE"},
};

static void MotTestRequestLoad(void);
static void MotTestLoadCtrl(void);
static void MotTestSetAnim(void);
static void MotTestDraw3D(void);
static void MotTestDrawMenu(void);
static void MotTestPadCtrl(void);

static int MotTestAnimCount(u_int anm_no)
{
    int num;
    ANI_CODE **tbl;

    tbl = anm_tbl[anm_no];

    if (tbl == NULL)
    {
        return 0;
    }

    for (num = 0; tbl[num] != NULL; num++)
    {
        ;
    }

    return num;
}

static int MotTestFrameCount(u_int *mot_p)
{
    MOT_TEST_MOT_HEADER *mfh;

    if (mot_p == NULL)
    {
        return 1;
    }

    mfh = (MOT_TEST_MOT_HEADER *)mot_p;

    if (mfh->frame_num == 0)
    {
        return 1;
    }

    return mfh->frame_num;
}

static void MotTestSetVector(sceVu0FVECTOR v, float x, float y, float z, float w)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
}

static void MotTestSetCamera(void)
{
    SgCAMERA *cam;

    cam = &mot_test_wrk.camera;
    memset(cam, 0, sizeof(*cam));

    MotTestSetVector(cam->p, 0.0f, mot_test_wrk.height, mot_test_wrk.zoom, 1.0f);
    MotTestSetVector(cam->i, 0.0f, mot_test_wrk.height, 0.0f, 1.0f);

    cam->roll = PI;
    cam->fov = DEG2RAD(44.0f);
    cam->nearz = 0.1f;
    cam->farz = 32768.0f;
    cam->ax = 1.0f;
    cam->ay = 0.40689999f;
    cam->cx = 2048.0f;
    cam->cy = 2048.0f;
    cam->zmin = 0.0f;
    cam->zmax = 16777215.0f;

    SgSetRefCamera(cam);
}

static void MotTestSetLight(void)
{
    static SgLIGHT light[1];
    sceVu0FVECTOR ambient = {0.35f, 0.35f, 0.35f, 1.0f};

    memset(light, 0, sizeof(light));

    light[0].direction[0] = -0.35f;
    light[0].direction[1] = -0.65f;
    light[0].direction[2] = -0.55f;
    light[0].direction[3] = 0.0f;
    light[0].diffuse[0] = mot_test_wrk.light_power;
    light[0].diffuse[1] = mot_test_wrk.light_power;
    light[0].diffuse[2] = mot_test_wrk.light_power;
    light[0].diffuse[3] = 1.0f;
    light[0].specular[0] = mot_test_wrk.light_power;
    light[0].specular[1] = mot_test_wrk.light_power;
    light[0].specular[2] = mot_test_wrk.light_power;
    light[0].specular[3] = 1.0f;
    light[0].Enable = 1;
    light[0].num = 1;

    SgSetAmbient(ambient);
    SgSetInfiniteLights(mot_test_wrk.camera.zd, light, 1);
    SgSetPointLights(NULL, 0);
    SgSetSpotLights(NULL, 0);
}

static void MotTestSetSquare(int pri, float x, float y, float w, float h, u_char r, u_char g, u_char b, u_char a)
{
    SetSquare(pri, x - 320.0f, y - 224.0f, (x - 320.0f) + w, y - 224.0f,
              x - 320.0f, (y - 224.0f) + h, (x - 320.0f) + w, (y - 224.0f) + h,
              r, g, b, a);
}

static void MotTestClampFloat(float *value, float min, float max)
{
    if (*value < min)
    {
        *value = min;
    }
    else if (*value > max)
    {
        *value = max;
    }
}

static void MotTestWrapInt(int *value, int min, int max)
{
    if (*value < min)
    {
        *value = max;
    }
    else if (*value > max)
    {
        *value = min;
    }
}

static u_int *MotTestAniModelPointer(u_short mdl_no)
{
    if (mdl_no == M000_MIKU || (mdl_no == M001_MAFUYU && plyr_init_ctrl.msn_no == 0))
    {
        return pmanmpk[mdl_no];
    }

    return pmanmodel[mdl_no];
}

static void MotTestSetAnim(void)
{
    HeaderSection *hs;
    const MOT_TEST_ENTRY *entry;

    entry = &mot_test_entries[mot_test_wrk.entry_no];

    mot_test_wrk.anim_num = MotTestAnimCount(entry->anm_no);

    if (mot_test_wrk.anim_num <= 0)
    {
        mot_test_wrk.anim_num = 1;
        mot_test_wrk.anim_no = 0;
        mot_test_wrk.frame = 0;
        mot_test_wrk.frame_num = 1;
        return;
    }

    if (mot_test_wrk.anim_no >= mot_test_wrk.anim_num)
    {
        mot_test_wrk.anim_no = mot_test_wrk.anim_num - 1;
    }

    motSetAnime(&mot_test_wrk.ani, anm_tbl[entry->anm_no], mot_test_wrk.anim_no);
    motInitInterpAnime(&mot_test_wrk.ani, 1);
    mot_test_wrk.ani.mot.reso = 1;

    hs = (HeaderSection *)mot_test_wrk.ani.base_p;
    motSetHierarchy(GetCoordP(hs), mot_test_wrk.ani.mot.dat);

    mot_test_wrk.frame = 0;
    mot_test_wrk.frame_num = MotTestFrameCount(mot_test_wrk.ani.mot.dat);
    mot_test_wrk.ani.mot.all_cnt = mot_test_wrk.frame_num;
}

static void MotTestSetupTexture(void)
{
    const MOT_TEST_ENTRY *entry;

    entry = &mot_test_entries[mot_test_wrk.entry_no];

    if (entry->mdl_no == M000_MIKU || entry->mdl_no == M001_MAFUYU)
    {
        if (pmanpk2[entry->mdl_no] != NULL)
        {
            SetManmdlTm2(pmanpk2[entry->mdl_no], 0, 1);
        }
    }
    else if (mot_test_wrk.ani.mdl_p != NULL)
    {
        SetEneVram(mot_test_wrk.ani.mdl_p, 0x2d00);
    }
}

static void MotTestRequestLoad(void)
{
    mot_test_wrk.loaded = 0;
    mot_test_wrk.load_step = MOT_TEST_LOAD_MODEL;
    mot_test_wrk.load_id = -1;
    mot_test_wrk.anim_no = 0;
    mot_test_wrk.frame = 0;
    mot_test_wrk.frame_num = 1;
}

static void MotTestLoadCtrl(void)
{
    const MOT_TEST_ENTRY *entry;
    u_int *mdl_p;
    u_int *anm_p;
    u_int *pkt_p;

    entry = &mot_test_entries[mot_test_wrk.entry_no];

    switch (mot_test_wrk.load_step)
    {
    case MOT_TEST_LOAD_MODEL:
        motInitMsn();
        InitEneVramCtrl();
        mot_test_wrk.load_id = LoadReq(M000_MIKU_MDL + entry->mdl_no, MODEL_ADDRESS);
        mot_test_wrk.load_step = MOT_TEST_WAIT_MODEL;
        break;
    case MOT_TEST_WAIT_MODEL:
        if (IsLoadEnd(mot_test_wrk.load_id) == 0)
        {
            break;
        }

        motInitEnemyMdl((u_int *)MikuPan_GetHostAddress(MODEL_ADDRESS), entry->mdl_no);
        mot_test_wrk.load_step = MOT_TEST_LOAD_ANIM;
        break;
    case MOT_TEST_LOAD_ANIM:
        mot_test_wrk.load_id = LoadReq(M000_MIKU_ANM + entry->anm_no, ANIM_ADDRESS);
        mot_test_wrk.load_step = MOT_TEST_WAIT_ANIM;
        break;
    case MOT_TEST_WAIT_ANIM:
        if (IsLoadEnd(mot_test_wrk.load_id) == 0)
        {
            break;
        }

        mdl_p = MotTestAniModelPointer(entry->mdl_no);
        anm_p = (u_int *)MikuPan_GetHostAddress(ANIM_ADDRESS);
        pkt_p = GetPakTaleAddr(anm_p);

        motInitAniCtrl(&mot_test_wrk.ani, anm_p, mdl_p, pkt_p, entry->mdl_no, entry->anm_no);
        MotTestSetAnim();
        MotTestSetupTexture();

        mot_test_wrk.loaded = 1;
        mot_test_wrk.load_step = MOT_TEST_READY;
        break;
    case MOT_TEST_READY:
        break;
    }
}

static void MotTestAdvanceFrame(void)
{
    if (mot_test_wrk.play == 0 || mot_test_wrk.frame_num <= 1)
    {
        return;
    }

    mot_test_wrk.frame++;

    if (mot_test_wrk.frame >= mot_test_wrk.frame_num)
    {
        if (mot_test_wrk.loop != 0)
        {
            mot_test_wrk.frame = 0;
        }
        else
        {
            mot_test_wrk.frame = mot_test_wrk.frame_num - 1;
            mot_test_wrk.play = 0;
        }
    }
}

static void MotTestDrawModel(void)
{
    HeaderSection *hs;
    SgCOORDUNIT *cp;
    const MOT_TEST_ENTRY *entry;
    float scale;

    if (mot_test_wrk.loaded == 0 || mot_test_wrk.ani.base_p == NULL)
    {
        return;
    }

    entry = &mot_test_entries[mot_test_wrk.entry_no];
    hs = (HeaderSection *)mot_test_wrk.ani.base_p;

    motSetCoordFrame(&mot_test_wrk.ani, mot_test_wrk.frame);

    if (mot_test_wrk.ani.mim_num != 0 || mot_test_wrk.ani.wmim_num != 0 || mot_test_wrk.ani.bg_num != 0)
    {
        SceneMimSetVertex(&mot_test_wrk.ani, mot_test_wrk.frame);
    }

    cp = GetCoordP(hs);
    sceVu0UnitMatrix(cp->matrix);

    scale = manmdl_dat[entry->mdl_no].scale;

    if (scale <= 0.0f)
    {
        scale = 1.0f;
    }

    scale = 25.0f / scale;
    cp->matrix[0][0] = scale;
    cp->matrix[1][1] = scale;
    cp->matrix[2][2] = scale;
    cp->matrix[3][3] = 1.0f;

    sceVu0RotMatrixX(cp->matrix, cp->matrix, PI);
    sceVu0RotMatrixY(cp->matrix, cp->matrix, mot_test_wrk.rot_y);

    cp->matrix[3][0] = 0.0f;
    cp->matrix[3][1] = 0.0f;
    cp->matrix[3][2] = 0.0f;
    cp->matrix[3][3] = 1.0f;

    CalcCoordinate(cp, hs->blocks - 1);

    if (mot_test_wrk.ani.cloth_ctrl != NULL)
    {
        acsClothCtrl(&mot_test_wrk.ani, mot_test_wrk.ani.mpk_p, entry->mdl_no, 0);
    }

    ManmdlSetAlpha(hs, 0x7f);
    ManTexflush();
    SgSortUnitKind(hs, -1);

    if (entry->mdl_no == M000_MIKU || entry->mdl_no == M001_MAFUYU)
    {
        DrawGirlSubObj(mot_test_wrk.ani.mpk_p, 0x7f);
    }
    else
    {
        DrawEneSubObj(mot_test_wrk.ani.mpk_p, 0x7f, 0x7f);
    }

    MotTestAdvanceFrame();
}

static void MotTestDraw3D(void)
{
    objInit();
    MotTestSetCamera();
    ClearTextureCache();
    SgTEXTransEnable();
    SetEnvironment();
    SgSetFog(0.0f, 255.0f, 1000.0f, 10000.0f, 0, 0, 0);
    MotTestSetLight();
    MotTestDrawModel();
    CheckDMATrans();
}

static void MotTestPadCtrl(void)
{
    int changed;

    if (pad[0].one & 0x40)
    {
        OutGameModeChange(OUTGAME_MODE_TITLE);
        return;
    }

    if (pad[0].one & 0x800)
    {
        mot_test_wrk.play ^= 1;
    }

    if (pad[0].rpt & 0x4000)
    {
        mot_test_wrk.menu_csr++;
        MotTestWrapInt(&mot_test_wrk.menu_csr, 0, MOT_TEST_MENU_NUM - 1);
    }
    else if (pad[0].rpt & 0x1000)
    {
        mot_test_wrk.menu_csr--;
        MotTestWrapInt(&mot_test_wrk.menu_csr, 0, MOT_TEST_MENU_NUM - 1);
    }

    changed = 0;

    if (pad[0].rpt & 0x2000)
    {
        changed = 1;
    }
    else if (pad[0].rpt & 0x8000)
    {
        changed = -1;
    }

    if (changed != 0)
    {
        switch (mot_test_wrk.menu_csr)
        {
        case 0:
            mot_test_wrk.entry_no += changed;
            MotTestWrapInt(&mot_test_wrk.entry_no, 0, MOT_TEST_ENTRY_NUM - 1);
            MotTestRequestLoad();
            break;
        case 1:
            if (mot_test_wrk.loaded != 0)
            {
                mot_test_wrk.anim_no += changed;
                MotTestWrapInt(&mot_test_wrk.anim_no, 0, mot_test_wrk.anim_num - 1);
                MotTestSetAnim();
            }
            break;
        case 2:
            mot_test_wrk.play = 0;
            mot_test_wrk.frame += changed;
            MotTestWrapInt(&mot_test_wrk.frame, 0, mot_test_wrk.frame_num - 1);
            break;
        case 5:
            mot_test_wrk.rot_y += DEG2RAD(5.0f * changed);
            if (mot_test_wrk.rot_y > PI)
            {
                mot_test_wrk.rot_y -= PI * 2.0f;
            }
            else if (mot_test_wrk.rot_y < -PI)
            {
                mot_test_wrk.rot_y += PI * 2.0f;
            }
            break;
        case 6:
            mot_test_wrk.height += 25.0f * changed;
            MotTestClampFloat(&mot_test_wrk.height, -1400.0f, 600.0f);
            break;
        case 7:
            mot_test_wrk.zoom += 50.0f * changed;
            MotTestClampFloat(&mot_test_wrk.zoom, -3000.0f, -200.0f);
            break;
        case 8:
            mot_test_wrk.light_power += 0.05f * changed;
            MotTestClampFloat(&mot_test_wrk.light_power, 0.05f, 2.0f);
            break;
        default:
            break;
        }
    }

    if (pad[0].one & 0x20)
    {
        switch (mot_test_wrk.menu_csr)
        {
        case 3:
            mot_test_wrk.play ^= 1;
            break;
        case 4:
            mot_test_wrk.loop ^= 1;
            break;
        case 9:
            MotTestRequestLoad();
            break;
        case 10:
            OutGameModeChange(OUTGAME_MODE_TITLE);
            break;
        default:
            break;
        }
    }
}

static void MotTestDrawMenuLine(int line, char *str)
{
    SetASCIIString2(1, 40.0f, line * 14 + 48, 1, 0x80, 0x80, 0x80, str);
}

static void MotTestDrawMenu(void)
{
    char tmp_str[256];
    const MOT_TEST_ENTRY *entry;
    char *state_str;

    entry = &mot_test_entries[mot_test_wrk.entry_no];

    SetASCIIString2(1, 40.0f, 28.0f, 0, 0x20, 0x80, 0x80, "MOTION TEST");

    if (mot_test_wrk.load_step == MOT_TEST_READY)
    {
        state_str = "READY";
    }
    else if (mot_test_wrk.load_step == MOT_TEST_WAIT_MODEL)
    {
        state_str = "LOAD MODEL";
    }
    else if (mot_test_wrk.load_step == MOT_TEST_WAIT_ANIM)
    {
        state_str = "LOAD ANIM";
    }
    else
    {
        state_str = "LOAD REQ";
    }

    sprintf(tmp_str, "PACK  %02d/%02d  MDL %02d ANM %02d  %s",
            mot_test_wrk.entry_no, MOT_TEST_ENTRY_NUM - 1, entry->mdl_no, entry->anm_no, entry->name);
    MotTestDrawMenuLine(0, tmp_str);

    sprintf(tmp_str, "ANIM  %02d/%02d", mot_test_wrk.anim_no, mot_test_wrk.anim_num - 1);
    MotTestDrawMenuLine(1, tmp_str);

    sprintf(tmp_str, "FRAME %04d/%04d", mot_test_wrk.frame, mot_test_wrk.frame_num - 1);
    MotTestDrawMenuLine(2, tmp_str);

    sprintf(tmp_str, "PLAY  %s", mot_test_wrk.play != 0 ? "ON" : "OFF");
    MotTestDrawMenuLine(3, tmp_str);

    sprintf(tmp_str, "LOOP  %s", mot_test_wrk.loop != 0 ? "ON" : "OFF");
    MotTestDrawMenuLine(4, tmp_str);

    sprintf(tmp_str, "ROT Y %+0.2f", mot_test_wrk.rot_y);
    MotTestDrawMenuLine(5, tmp_str);

    sprintf(tmp_str, "HEIGHT %+0.1f", mot_test_wrk.height);
    MotTestDrawMenuLine(6, tmp_str);

    sprintf(tmp_str, "ZOOM  %+0.1f", mot_test_wrk.zoom);
    MotTestDrawMenuLine(7, tmp_str);

    sprintf(tmp_str, "LIGHT %0.2f", mot_test_wrk.light_power);
    MotTestDrawMenuLine(8, tmp_str);

    sprintf(tmp_str, "RELOAD");
    MotTestDrawMenuLine(9, tmp_str);

    sprintf(tmp_str, "EXIT");
    MotTestDrawMenuLine(10, tmp_str);

    sprintf(tmp_str, "STATE %s   X DECIDE  O EXIT  START PLAY", state_str);
    SetASCIIString2(1, 40.0f, 410.0f, 1, 0x80, 0x80, 0x80, tmp_str);

    MotTestSetSquare(2, 36.0f, mot_test_wrk.menu_csr * 14 + 46, 420.0f, 12.0f, 0x50, 0x50, 0x64, 0x50);
}

void MotTestInit(void)
{
    memset(&mot_test_wrk, 0, sizeof(mot_test_wrk));

    gra2dInitST();
    Init3D();
    InitialDmaBuffer();
    InitEneVramCtrl();
    motInitMsn();

    mot_test_wrk.entry_no = 0;
    mot_test_wrk.anim_no = 0;
    mot_test_wrk.anim_num = 1;
    mot_test_wrk.frame_num = 1;
    mot_test_wrk.play = 1;
    mot_test_wrk.loop = 1;
    mot_test_wrk.height = -450.0f;
    mot_test_wrk.zoom = -1600.0f;
    mot_test_wrk.light_power = 0.95f;

    MotTestRequestLoad();
}

void MotTestCtrl(void)
{
    MotTestLoadCtrl();
    MotTestPadCtrl();
    MotTestDraw3D();
    MotTestDrawMenu();
    gra2dDraw(GRA2D_CALL_OG);
}
