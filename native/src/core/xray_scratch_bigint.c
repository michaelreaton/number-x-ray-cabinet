#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && defined(_MSC_VER)
#include <process.h>
typedef void *XrayWinHandle;
typedef void *XrayWinThreadpoolWork;
typedef void *XrayWinThreadpoolInstance;
typedef void (__stdcall *XrayWinThreadpoolWorkCallback)(
  XrayWinThreadpoolInstance instance,
  void *context,
  XrayWinThreadpoolWork work);
__declspec(dllimport) unsigned long __stdcall WaitForMultipleObjects(
  unsigned long nCount,
  const XrayWinHandle *lpHandles,
  int bWaitAll,
  unsigned long dwMilliseconds);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(
  XrayWinHandle hHandle,
  unsigned long dwMilliseconds);
__declspec(dllimport) int __stdcall CloseHandle(XrayWinHandle hObject);
__declspec(dllimport) XrayWinHandle __stdcall CreateEventA(
  void *lpEventAttributes,
  int bManualReset,
  int bInitialState,
  const char *lpName);
__declspec(dllimport) int __stdcall SetEvent(XrayWinHandle hEvent);
__declspec(dllimport) int __stdcall ResetEvent(XrayWinHandle hEvent);
__declspec(dllimport) XrayWinThreadpoolWork __stdcall CreateThreadpoolWork(
  XrayWinThreadpoolWorkCallback pfnwk,
  void *pv,
  void *pcbe);
__declspec(dllimport) void __stdcall SubmitThreadpoolWork(XrayWinThreadpoolWork pwk);
__declspec(dllimport) void __stdcall WaitForThreadpoolWorkCallbacks(
  XrayWinThreadpoolWork pwk,
  int fCancelPendingCallbacks);
__declspec(dllimport) void __stdcall CloseThreadpoolWork(XrayWinThreadpoolWork pwk);
#define XRAY_WIN_WAIT_INFINITE 0xffffffffUL
#define XRAY_WIN_MAXIMUM_WAIT_OBJECTS 64U
#define XRAY_NTT16_TRANSFORM_POOL_WORKERS 8U
#define XRAY_TOOM_POINT_POOL_WORKERS 5U
#endif

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#define XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS 1
#elif defined(__SIZEOF_INT128__)
#define XRAY_BIGINT_HAS_UINT128 1
#else
#error "The 64-bit scratch bigint core requires __uint128 or MSVC x64 128-bit intrinsics."
#endif

#if defined(_MSC_VER)
#define XRAY_BIGINT_HAS_THREAD_LOCAL 1
#define XRAY_BIGINT_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__)
#define XRAY_BIGINT_HAS_THREAD_LOCAL 1
#define XRAY_BIGINT_THREAD_LOCAL __thread
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define XRAY_BIGINT_HAS_THREAD_LOCAL 1
#define XRAY_BIGINT_THREAD_LOCAL _Thread_local
#else
#define XRAY_BIGINT_HAS_THREAD_LOCAL 0
#define XRAY_BIGINT_THREAD_LOCAL
#endif

#define XRAY_BIGINT_WORD_BITS 64U
#define XRAY_BIGINT_DECIMAL_CHUNK_BASE 1000000000U
#define XRAY_BIGINT_DECIMAL_CHUNK_DIGITS 9U
#define XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT UINT64_C(18446744073)
#define XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER 709551616U
#define XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE UINT64_C(10000000000000000000)
#define XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS 19U
#define XRAY_BIGINT_DECIMAL_WIDE_CHUNK_PREINVERSE UINT64_C(15581492618384294730)
/* UINT64_MAX / 1e9, matching reciprocal_u32 for the decimal chunk divisor. */
#define XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL UINT64_C(18446744073)
#define XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS 48U
#define XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS 8U
#define XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS 54U
#define XRAY_BIGINT_DECIMAL_PREINV_PAIR_MIN_EST_DIGITS 1001U
#define XRAY_BIGINT_DECIMAL_PREINV_PAIR_MAX_EST_DIGITS 1001U
#define XRAY_BIGINT_DECIMAL_DC_DIRECT_SMALL_EST_DIGITS 151U
#define XRAY_BIGINT_DECIMAL_DC_DIRECT_SMALL_LEAF_CHUNKS 16U
#define XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS 192U
#define XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS 8U
#define XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_LEAF32_MIN_WIDE_CHUNKS 384U
#define XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_MAX_WIDE_CHUNKS 512U
#define XRAY_BIGINT_DECIMAL_DC_STATIC_DIRECT_MAX_WIDE_CHUNKS 512U
#define XRAY_BIGINT_PARSE_CHUNK_DIGITS 19U
#define XRAY_BIGINT_PARSE_LARGE_MIN_DIGITS 2048U
#define XRAY_BIGINT_PARSE_LARGE_CHUNK_DIGITS 15U
#define XRAY_BIGINT_KARATSUBA_THRESHOLD 64U
#define XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS 8U
#define XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS 512U
#define XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS 8U
#define XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS 16U
#define XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR 4U
#define XRAY_BIGINT_SPARSE_MUL_MIN_LIMBS 16U
#define XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR 4U
#define XRAY_BIGINT_SPARSE_MUL_MIN_PRODUCTS 64U
#define XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MIN_LIMBS 32U
#define XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX 16U
#define XRAY_BIGINT_SPARSE_STACK_INDEX_CAP 64U
#define XRAY_BIGINT_DENSE_TOOM4_L64D2_MIN_LIMBS 400U
#define XRAY_BIGINT_DENSE_TOOM4_L64D2_MAX_LIMBS 3600U
#define XRAY_BIGINT_DENSE_TOOM3_COMBO_UPPER_MIN_LIMBS 768U
#define XRAY_BIGINT_DENSE_TOOM4_TOP_MIN_LIMBS 1200U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_LOW_MAX_LIMBS 1500U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_MIN_LIMBS 1500U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_MAX_LIMBS 1800U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_UPPER_MIN_LIMBS 2400U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_UPPER_MAX_LIMBS 3000U
#define XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_GROUPS 4U
#define XRAY_BIGINT_DENSE_NTT16_MIN_LIMBS 15000U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM3_MIN_LIMBS 200U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM3_MAX_LIMBS 1500U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM3_LOW_LEAF_CUT_LIMBS 260U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM3_MID_LEAF_CUT_LIMBS 320U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM3_UPPER_LEAF_CUT_LIMBS 1200U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MIN_LIMBS 400U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MAX_LIMBS 1800U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_LOW_LEAF_CUT_LIMBS 500U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MID_LEAF_CUT_LIMBS 800U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_UPPER_LEAF_CUT_LIMBS 1500U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_MIN_LIMBS 420U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_MAX_LIMBS 440U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_LEAF_LIMBS 72U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_GROUPS 5U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_MIN_LIMBS 208U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_MAX_LIMBS 240U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_LEAF_LIMBS 52U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_DEPTH 1U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_MIN_LIMBS 420U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_MAX_LIMBS 440U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_LEAF_LIMBS 44U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_DEPTH 1U
#define XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_GROUPS 4U
#define XRAY_BIGINT_DENSE_SQUARE_TOOM4_MIN_LIMBS 2400U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_MIN_LIMBS 1500U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_MAX_LIMBS 1800U
#define XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_GROUPS 4U
#define XRAY_BIGINT_DENSE_SQUARE_NTT16_MIN_LIMBS 15000U
#define XRAY_BIGINT_NTT16_BASE_BITS 16U
#define XRAY_BIGINT_NTT16_BASE_MASK UINT64_C(0xffff)
#define XRAY_BIGINT_NTT16_MAX_POWER 21U
#define XRAY_BIGINT_NTT16_MAX_LENGTH (1ULL << XRAY_BIGINT_NTT16_MAX_POWER)
#define XRAY_BIGINT_NTT16_MOD0 998244353U
#define XRAY_BIGINT_NTT16_MOD1 1004535809U
#define XRAY_BIGINT_NTT32_MOD2 469762049U
#define XRAY_BIGINT_NTT16_ROOT 3U
#define XRAY_BIGINT_NTT32_MAX_SAFE_DIGITS 25536448U
#define XRAY_BIGINT_FERMAT_65537 65537U
#define XRAY_BIGINT_DIVEXACT_3_INVERSE UINT64_C(0xAAAAAAAAAAAAAAAB)
#define XRAY_BIGINT_DIVEXACT_5_INVERSE UINT64_C(0xCCCCCCCCCCCCCCCD)
#define XRAY_TOOM3_INTERP_SHIFT_DIV2 1U
#define XRAY_TOOM3_INTERP_EXACT_DIV3 2U
#define XRAY_TOOM3_INTERP_INPLACE_DIV 4U
#define XRAY_TOOM3_INTERP_EVAL_NEG2 16U
#define XRAY_TOOM4_INTERP_FACTORED_DIV 8U

static const uint64_t parse_decimal_powers[] = {
  UINT64_C(1),
  UINT64_C(10),
  UINT64_C(100),
  UINT64_C(1000),
  UINT64_C(10000),
  UINT64_C(100000),
  UINT64_C(1000000),
  UINT64_C(10000000),
  UINT64_C(100000000),
  UINT64_C(1000000000),
  UINT64_C(10000000000),
  UINT64_C(100000000000),
  UINT64_C(1000000000000),
  UINT64_C(10000000000000),
  UINT64_C(100000000000000),
  UINT64_C(1000000000000000),
  UINT64_C(10000000000000000),
  UINT64_C(100000000000000000),
  UINT64_C(1000000000000000000),
  UINT64_C(10000000000000000000)
};

static const char decimal_digit_pairs[] =
  "00010203040506070809"
  "10111213141516171819"
  "20212223242526272829"
  "30313233343536373839"
  "40414243444546474849"
  "50515253545556575859"
  "60616263646566676869"
  "70717273747576777879"
  "80818283848586878889"
  "90919293949596979899";

static const uint64_t decimal_dc_static_pow2_0[] = {
  UINT64_C(10000000000000000000)
};

static const uint64_t decimal_dc_static_pow2_1[] = {
  UINT64_C(687399551400673280), UINT64_C(5421010862427522170)
};

static const uint64_t decimal_dc_static_pow2_2[] = {
  UINT64_C(0), UINT64_C(8607968719199866880), UINT64_C(532749306367912313), UINT64_C(1593091911132452277)
};

static const uint64_t decimal_dc_static_pow2_3[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(15252863918154973184), UINT64_C(4477131725245556545),
  UINT64_C(6853971483050138908), UINT64_C(15193086134719162827), UINT64_C(11744654113764246714), UINT64_C(137582102682973977)
};

static const uint64_t decimal_dc_static_pow2_4[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(18104751977006104576), UINT64_C(7022382078611961980), UINT64_C(5818713298520399752), UINT64_C(3845577022746030748),
  UINT64_C(12139943004249278607), UINT64_C(5889242339408380438), UINT64_C(14552017514495643209), UINT64_C(4968216578135424189),
  UINT64_C(15773629750718909556), UINT64_C(9342075265934726546), UINT64_C(1149859480163044520), UINT64_C(1026134200324594)
};

static const uint64_t decimal_dc_static_pow2_5[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(14812487063030464512), UINT64_C(15678693317147271784), UINT64_C(13223892582731984624),
  UINT64_C(17465812138978755258), UINT64_C(7664081700110503653), UINT64_C(7867086747378126271), UINT64_C(6694233273289526891),
  UINT64_C(979072618450083183), UINT64_C(3181249480858999896), UINT64_C(1618460960086060797), UINT64_C(9729852571323393537),
  UINT64_C(5512821131327516378), UINT64_C(14040226153079509991), UINT64_C(5675458110915889831), UINT64_C(14808706736072332751),
  UINT64_C(16738973592517505687), UINT64_C(3211101337243187422), UINT64_C(16112068347911025940), UINT64_C(7727566944689383997),
  UINT64_C(16383823567047475423), UINT64_C(9390551701925100160), UINT64_C(10351412666324596833), UINT64_C(57080609611)
};

static const uint64_t decimal_dc_static_pow2_6[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(319005509880073473),
  UINT64_C(1806551507373502735), UINT64_C(11302742870345376530), UINT64_C(13258527722524180429), UINT64_C(5446412568905165168),
  UINT64_C(12543982081940419832), UINT64_C(15403731642616081653), UINT64_C(4737951089047661062), UINT64_C(3794326284714172484),
  UINT64_C(2300983730994112517), UINT64_C(13011946269911379846), UINT64_C(3105983672841008074), UINT64_C(3271341497136365229),
  UINT64_C(9317620278607948968), UINT64_C(6461467202444947841), UINT64_C(12956296021901158792), UINT64_C(5590221285761126278),
  UINT64_C(5901497884083299244), UINT64_C(15598774421181943027), UINT64_C(12767720238382953525), UINT64_C(985519523759711351),
  UINT64_C(9736924633090785807), UINT64_C(5815317455127115990), UINT64_C(10640367891977486574), UINT64_C(3341215565587611947),
  UINT64_C(3379075655803910730), UINT64_C(7729393315787486022), UINT64_C(10660439658308642124), UINT64_C(2736012397475497809),
  UINT64_C(15080284521096067759), UINT64_C(15230264543647898301), UINT64_C(16946947390220236831), UINT64_C(18357957862637485458),
  UINT64_C(8751981732964013983), UINT64_C(7264578455454564054), UINT64_C(16667145575142581624), UINT64_C(16393813431037489643),
  UINT64_C(14639777417809856269), UINT64_C(17573061233865061950), UINT64_C(17498971390803385829), UINT64_C(16526120241226747429),
  UINT64_C(14725285770136587064), UINT64_C(7748610182331927902), UINT64_C(11569036654566192642), UINT64_C(176)
};

/* Common D&C split powers: (10^19)^108 and (10^19)^216. */
static const uint64_t decimal_dc_static_split_108[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(11492601575593879312), UINT64_C(2432930250738995092), UINT64_C(11250032868119446614), UINT64_C(12311509281492867237),
  UINT64_C(8142193255152826466), UINT64_C(17649124042404922197), UINT64_C(8506992211107192032), UINT64_C(5308693518345070329),
  UINT64_C(11657617424957150115), UINT64_C(9736864656011772025), UINT64_C(18418366969572012438), UINT64_C(3835085718073837839),
  UINT64_C(8786586887990547552), UINT64_C(12916066778197683232), UINT64_C(1779593848892368876), UINT64_C(4113857022322717381),
  UINT64_C(2213661233606375890), UINT64_C(6275266552531494904), UINT64_C(1892666161577107751), UINT64_C(4148069857493457654),
  UINT64_C(710816888508012453), UINT64_C(13540903256687490615), UINT64_C(10402586779319131144), UINT64_C(248022429357673964),
  UINT64_C(11641474857609273062), UINT64_C(3742787389229817601), UINT64_C(12393884196774965829), UINT64_C(8567493209855885032),
  UINT64_C(8788673300584878760), UINT64_C(1712748865133270718), UINT64_C(10292331007682671485), UINT64_C(111527031356468177),
  UINT64_C(941486497040290970), UINT64_C(10451443648619761059), UINT64_C(15506653373995513283), UINT64_C(378604142518798005),
  UINT64_C(10676529593383460110), UINT64_C(6353366813846183271), UINT64_C(14562000144835128825), UINT64_C(14037492106836798521),
  UINT64_C(12462699833123441739), UINT64_C(13994399201629111565), UINT64_C(5136726508743836122), UINT64_C(8157413941856242589),
  UINT64_C(7516751097465442006), UINT64_C(17354538185732239709), UINT64_C(18192046689499857181), UINT64_C(707173263913549217),
  UINT64_C(13096401271187362339), UINT64_C(14785493609587622490), UINT64_C(14270060064914068180), UINT64_C(3254567331098147820),
  UINT64_C(936468917851403129), UINT64_C(459646262099376303), UINT64_C(10815765764015996153), UINT64_C(1747892108241650799),
  UINT64_C(4436655997975468875), UINT64_C(17895246071599205852), UINT64_C(6492640419474721416), UINT64_C(2527883622497076754),
  UINT64_C(15385803755835834612), UINT64_C(17369432036438825238), UINT64_C(6673415186352893389), UINT64_C(2580847080841170283),
  UINT64_C(14789037167189541821), UINT64_C(11521325880544843031), UINT64_C(11621830088647162076), UINT64_C(3106601910772486403),
  UINT64_C(9152222862823496910), UINT64_C(187448992598318289), UINT64_C(10286881249308797052), UINT64_C(14620156726737567925),
  UINT64_C(7605438164920111985), UINT64_C(17188782079672017055), UINT64_C(6493957103)
};

static const uint64_t decimal_dc_static_split_216[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(7347532975526240512), UINT64_C(9800078666658107046), UINT64_C(11992965144960434071), UINT64_C(15257751403840477149),
  UINT64_C(16085338471453854588), UINT64_C(5589542823890586176), UINT64_C(3321802613300621971), UINT64_C(12783520209924015876),
  UINT64_C(904050316064085106), UINT64_C(14627116000546553101), UINT64_C(17184054329021829564), UINT64_C(6386192301831945152),
  UINT64_C(7520724516868876482), UINT64_C(16138284047519155325), UINT64_C(5900162730196635416), UINT64_C(2078955671217501687),
  UINT64_C(15099659972107027011), UINT64_C(1068002970705007216), UINT64_C(12212158987425507335), UINT64_C(9933349902798966964),
  UINT64_C(9017328326248020958), UINT64_C(2089893016970269622), UINT64_C(10292752943384465984), UINT64_C(10628227280243134048),
  UINT64_C(15444256278118118895), UINT64_C(16900932870160792825), UINT64_C(8108396211555557082), UINT64_C(5849886600287896450),
  UINT64_C(15560268731885127260), UINT64_C(12013063878413432624), UINT64_C(6634409019915288924), UINT64_C(2223374930974857378),
  UINT64_C(1009902257145172415), UINT64_C(5310798116985301751), UINT64_C(14280687402826714755), UINT64_C(3262040221679533681),
  UINT64_C(8529138648230006571), UINT64_C(1649866200357955234), UINT64_C(4339330758389064141), UINT64_C(4744090909977199063),
  UINT64_C(4978763099145276212), UINT64_C(3344999003132845590), UINT64_C(9833970860906880005), UINT64_C(1427993782152729971),
  UINT64_C(15833444520128402648), UINT64_C(3858290313685404847), UINT64_C(3121704486664311336), UINT64_C(18360902518115256625),
  UINT64_C(7389185632934990713), UINT64_C(7043012801142057632), UINT64_C(2298407361839131213), UINT64_C(299580088482456581),
  UINT64_C(3003922619785291189), UINT64_C(17038739243599262660), UINT64_C(13415697691412214697), UINT64_C(10259831308852042270),
  UINT64_C(10046812416602165455), UINT64_C(6079490524011188230), UINT64_C(6531531156125706208), UINT64_C(1465543561565240084),
  UINT64_C(2012822273840508753), UINT64_C(3992234772428132220), UINT64_C(6020381196580703325), UINT64_C(4359637515971405555),
  UINT64_C(11791549352769612785), UINT64_C(6343493803786841647), UINT64_C(11531387310030857534), UINT64_C(4718755979345659590),
  UINT64_C(9999232856298626784), UINT64_C(3298453765072139928), UINT64_C(3870348215015110078), UINT64_C(1927684684718308366),
  UINT64_C(13002013453928141482), UINT64_C(18405976232027875613), UINT64_C(8674387406369825874), UINT64_C(12600036273501273359),
  UINT64_C(6574395661633627816), UINT64_C(12004850488310583794), UINT64_C(8374073166631729726), UINT64_C(735376877296900897),
  UINT64_C(400570100038571390), UINT64_C(9100118601416027892), UINT64_C(3959626356570493250), UINT64_C(4841914436125188538),
  UINT64_C(3118129256711318580), UINT64_C(9905093393147201415), UINT64_C(3992736954135599625), UINT64_C(1939796410890869936),
  UINT64_C(2983747229745327504), UINT64_C(14300100212402023274), UINT64_C(1981981214743702435), UINT64_C(11848005902804513868),
  UINT64_C(10990343432391082456), UINT64_C(6082709346868764951), UINT64_C(12934792992679980687), UINT64_C(5876854190107899017),
  UINT64_C(17819176573563393887), UINT64_C(4379951603302330730), UINT64_C(6438608632321541273), UINT64_C(14922014325254082839),
  UINT64_C(17442264916702063136), UINT64_C(12812777873822861545), UINT64_C(13534259750880726030), UINT64_C(14895497725922591768),
  UINT64_C(12639170170415763384), UINT64_C(2404059091878754126), UINT64_C(8604205519138812358), UINT64_C(2108254673731331213),
  UINT64_C(17085383028827520626), UINT64_C(3912301628452865667), UINT64_C(12820950096246936532), UINT64_C(16287510017984228865),
  UINT64_C(8730844806920750840), UINT64_C(10292262449973145632), UINT64_C(11515142108341376782), UINT64_C(9922965709069673348),
  UINT64_C(18054147328661406362), UINT64_C(9091697553131668918), UINT64_C(9425179805126714371), UINT64_C(8259085603546314781),
  UINT64_C(4594466890603316476), UINT64_C(12348700734747617229), UINT64_C(4670830365068093470), UINT64_C(15560001156978252914),
  UINT64_C(1311149952269475570), UINT64_C(12446074387249590600), UINT64_C(15932975478659798374), UINT64_C(14574847659582190656),
  UINT64_C(8190935540580699365), UINT64_C(2968827650363115599), UINT64_C(9308949476733383404), UINT64_C(16022965081698359749),
  UINT64_C(8787290507186163170), UINT64_C(3001907879787941661), UINT64_C(2066438261593320283), UINT64_C(8347702825783909296),
  UINT64_C(7944707543862792113), UINT64_C(5346983429624486692), UINT64_C(7314543693908763651), UINT64_C(3218200348061871569),
  UINT64_C(17049715814826698065), UINT64_C(15073461432107006275), UINT64_C(17820668834569580737), UINT64_C(16055674153631407377),
  UINT64_C(3527095084789071744), UINT64_C(8897476119876929475), UINT64_C(15026800916295171536), UINT64_C(2888658083926845378),
  UINT64_C(5277990720287262433), UINT64_C(2)
};

static XrayScratchBigInt decimal_dc_static_ladder_values[] = {
  {(uint64_t *)decimal_dc_static_pow2_0, 1U, 1U},
  {(uint64_t *)decimal_dc_static_pow2_1, 2U, 2U},
  {(uint64_t *)decimal_dc_static_pow2_2, 4U, 4U},
  {(uint64_t *)decimal_dc_static_pow2_3, 8U, 8U},
  {(uint64_t *)decimal_dc_static_pow2_4, 16U, 16U},
  {(uint64_t *)decimal_dc_static_pow2_5, 32U, 32U},
  {(uint64_t *)decimal_dc_static_pow2_6, 64U, 64U},
};

XrayBigIntRouteConfig xray_bigint_route_config(void) {
  XrayBigIntRouteConfig config;
  config.word_bits = XRAY_BIGINT_WORD_BITS;
  config.karatsuba_threshold_limbs = XRAY_BIGINT_KARATSUBA_THRESHOLD;
  config.decimal_horner_min_limbs = XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS;
  config.mul_unroll4_route_min_limbs = XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
  config.mul_unroll4_route_max_limbs = XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  config.mul_unroll4_route_enabled = 1;
  config.msvc_uint128_helpers = 1;
#else
  config.mul_unroll4_route_enabled = 0;
  config.msvc_uint128_helpers = 0;
#endif
  return config;
}

char *xray_bigint_route_config_json(void) {
  const size_t capacity = 4096U;
  char *json = (char *)calloc(capacity, 1);
  if (!json) return NULL;
  XrayBigIntRouteConfig config = xray_bigint_route_config();
  int written = snprintf(
    json,
    capacity,
    "{\"wordBits\":%u"
    ",\"karatsubaThresholdLimbs\":%zu"
    ",\"decimalHornerMinLimbs\":%zu"
    ",\"decimalPairWriterSmallMaxLimbs\":%u"
    ",\"decimalPairWriterHornerMaxLimbs\":%u"
    ",\"decimalPreinvPairMinEstimatedDigits\":%u"
    ",\"decimalPreinvPairMaxEstimatedDigits\":%u"
    ",\"decimalDcMinWideChunks\":%u"
    ",\"decimalDcLeafChunks\":%u"
    ",\"decimalDcCachedPreinvMaxWideChunks\":%u"
    ",\"decimalDcStaticDirectMaxWideChunks\":%u"
    ",\"decimalDcStaticSplitChunks\":[108,216]"
    ",\"decimalWideChunkDigits\":%u"
    ",\"decimalWideChunkBase\":\"10000000000000000000\""
    ",\"decimalWideChunkPreinverse\":\"15581492618384294730\""
    ",\"parseChunkDigits\":%u"
    ",\"parseLargeMinDigits\":%u"
    ",\"parseLargeChunkDigits\":%u"
    ",\"parsePolicy\":\"19 digits below 2048 decimal digits; 15 digits at or above 2048 decimal digits\""
    ",\"mulUnroll4RouteMinLimbs\":%zu"
    ",\"mulUnroll4RouteMaxLimbs\":%zu"
    ",\"mulUnroll4RouteEnabled\":%s"
    ",\"msvcUint128Helpers\":%s"
    ",\"squareSelfMulMaxLimbs\":%u"
    ",\"squareTinySelfMulPolicy\":\"<=8 limbs\""
    ",\"decimalPairWriterPolicy\":\"small<=8 limbs, horner 48..54 limbs, D&C direct leaf16 for estimated 151-digit inputs, or cached D&C preinv then preinv base-1e19 pair writer fallback for estimated 1001-digit inputs\""
    ",\"decimalDcPolicy\":\"base-1e19 D&C cached preinv leaf16 through 512 wide chunks, ladder fallback above\""
    ",\"sparseSquareMinLimbs\":%u"
    ",\"sparseSquareDensityDivisor\":%u"
    ",\"sparseMulMinLimbs\":%u"
    ",\"sparseMulDensityDivisor\":%u"
    ",\"sparseMulMinProducts\":%u"
    ",\"sparseMulTinyProductsMinLimbs\":%u"
    ",\"sparseMulTinyProductsMax\":%u"
    ",\"fermat65537\":%u"
    ",\"productionRoutes\":["
      "{\"name\":\"karatsuba-mul\",\"thresholdLimbs\":%u},"
      "{\"name\":\"karatsuba-square\",\"thresholdLimbs\":%u},"
      "{\"name\":\"decimal-horner\",\"minLimbs\":%u},"
      "{\"name\":\"decimal-pair-writer\",\"smallMaxLimbs\":%u,\"hornerMaxLimbs\":%u},"
      "{\"name\":\"decimal-dc-direct-small-window\",\"estimatedDigits\":151,\"leafChunks\":16},"
      "{\"name\":\"decimal-cached-preinv1e19-window\",\"minEstimatedDigits\":%u,\"maxEstimatedDigits\":%u,\"leafChunks\":32,\"fallback\":\"decimal-preinv1e19-pair-window\"},"
      "{\"name\":\"decimal-dc-ladder\",\"minWideChunks\":%u,\"leafChunks\":%u,\"cachedPreinvMaxWideChunks\":%u,\"staticDirectMaxWideChunks\":%u,\"staticSplitChunks\":[108,216]},"
      "{\"name\":\"decimal-parse-large\",\"minDigits\":%u,\"chunkDigits\":%u},"
      "{\"name\":\"mul-unroll4\",\"enabled\":%s,\"minLimbs\":%zu,\"maxLimbs\":%zu},"
      "{\"name\":\"sparse-square\",\"minLimbs\":%u,\"densityDivisor\":%u},"
      "{\"name\":\"sparse-mul\",\"minLimbs\":%u,\"densityDivisor\":%u,\"minProducts\":%u,\"tinyProductsMinLimbs\":%u,\"tinyProductsMax\":%u}"
    "]"
    ",\"diagnosticProbeFamilies\":["
      "\"decimal-threshold\","
      "\"decimal-parse-chunk\","
      "\"decimal-divide-1e19\","
      "\"decimal-divide-1e19-preinv\","
      "\"decimal-dc-direct\","
      "\"decimal-dc-static\","
      "\"decimal-dc-workspace\","
      "\"decimal-dc-preinv-qhat\","
      "\"decimal-dc-cached-preinv\","
      "\"decimal-dc-parallel\","
      "\"mul-threshold\","
      "\"karatsuba-middle\","
      "\"karatsuba-workspace\","
      "\"toom3\","
      "\"toom3-split-view\","
      "\"toom3-workspace\","
      "\"toom3-full-workspace\","
      "\"toom3-full-workspace-div2\","
      "\"toom3-full-workspace-div3\","
      "\"toom3-full-workspace-div2-div3\","
      "\"sparse-shape\""
    "]"
    ",\"mpirGmpClue\":\"GMP separates decimal conversion into thresholded parse/format stages; Number X-Ray routes only same-run winners and keeps unproven formatter probes default-off.\""
    "}",
    config.word_bits,
    config.karatsuba_threshold_limbs,
    config.decimal_horner_min_limbs,
    XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS,
    XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS,
    XRAY_BIGINT_DECIMAL_PREINV_PAIR_MIN_EST_DIGITS,
    XRAY_BIGINT_DECIMAL_PREINV_PAIR_MAX_EST_DIGITS,
    XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_MAX_WIDE_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_STATIC_DIRECT_MAX_WIDE_CHUNKS,
    XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS,
    XRAY_BIGINT_PARSE_CHUNK_DIGITS,
    XRAY_BIGINT_PARSE_LARGE_MIN_DIGITS,
    XRAY_BIGINT_PARSE_LARGE_CHUNK_DIGITS,
    config.mul_unroll4_route_min_limbs,
    config.mul_unroll4_route_max_limbs,
    config.mul_unroll4_route_enabled ? "true" : "false",
    config.msvc_uint128_helpers ? "true" : "false",
    XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS,
    XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR,
    XRAY_BIGINT_SPARSE_MUL_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR,
    XRAY_BIGINT_SPARSE_MUL_MIN_PRODUCTS,
    XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX,
    XRAY_BIGINT_FERMAT_65537,
    XRAY_BIGINT_KARATSUBA_THRESHOLD,
    XRAY_BIGINT_KARATSUBA_THRESHOLD,
    XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS,
    XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS,
    XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS,
    XRAY_BIGINT_DECIMAL_PREINV_PAIR_MIN_EST_DIGITS,
    XRAY_BIGINT_DECIMAL_PREINV_PAIR_MAX_EST_DIGITS,
    XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_MAX_WIDE_CHUNKS,
    XRAY_BIGINT_DECIMAL_DC_STATIC_DIRECT_MAX_WIDE_CHUNKS,
    XRAY_BIGINT_PARSE_LARGE_MIN_DIGITS,
    XRAY_BIGINT_PARSE_LARGE_CHUNK_DIGITS,
    config.mul_unroll4_route_enabled ? "true" : "false",
    config.mul_unroll4_route_min_limbs,
    config.mul_unroll4_route_max_limbs,
    XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR,
    XRAY_BIGINT_SPARSE_MUL_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR,
    XRAY_BIGINT_SPARSE_MUL_MIN_PRODUCTS,
    XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MIN_LIMBS,
    XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX);
  if (written < 0 || (size_t)written >= capacity) {
    free(json);
    return NULL;
  }
  return json;
}

void xray_bigint_init(XrayScratchBigInt *value) {
  if (!value) return;
  value->limbs = NULL;
  value->count = 0;
  value->capacity = 0;
}

void xray_bigint_clear(XrayScratchBigInt *value) {
  if (!value) return;
  free(value->limbs);
  value->limbs = NULL;
  value->count = 0;
  value->capacity = 0;
}

static int reserve_limbs(XrayScratchBigInt *value, size_t capacity) {
  if (value->capacity >= capacity) return 1;
  size_t next_capacity = value->capacity ? value->capacity * 2 : 4;
  while (next_capacity < capacity) next_capacity *= 2;
  uint64_t *next = (uint64_t *)realloc(value->limbs, sizeof(uint64_t) * next_capacity);
  if (!next) return 0;
  value->limbs = next;
  value->capacity = next_capacity;
  return 1;
}

static void normalize(XrayScratchBigInt *value) {
  while (value->count > 0 && value->limbs[value->count - 1] == 0) value->count--;
}

static int set_u32(XrayScratchBigInt *value, uint32_t small) {
  if (!reserve_limbs(value, 1)) return 0;
  value->limbs[0] = (uint64_t)small;
  value->count = small ? 1 : 0;
  return 1;
}

static int set_u64(XrayScratchBigInt *value, uint64_t small) {
  if (!reserve_limbs(value, 1)) return 0;
  value->limbs[0] = small;
  value->count = small ? 1 : 0;
  return 1;
}

static int is_ascii_space(unsigned char ch) {
  return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

int xray_bigint_is_zero(const XrayScratchBigInt *value) {
  return !value || value->count == 0;
}

int xray_bigint_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) return 1;
  if (value->count == 0) {
    out->count = 0;
    return 1;
  }
  if (!reserve_limbs(out, value->count ? value->count : 1)) return 0;
  if (value->count) memcpy(out->limbs, value->limbs, sizeof(uint64_t) * value->count);
  out->count = value->count;
  return 1;
}

static uint64_t mul_add_small_word(uint64_t word, uint64_t multiplier, uint64_t carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(word, multiplier, &high);
  unsigned __int64 sum = low + carry;
  if (sum < low) high++;
  *out = (uint64_t)sum;
  return (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)word * (__uint128_t)multiplier + (__uint128_t)carry;
  *out = (uint64_t)product;
  return (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
#endif
}

static uint64_t mul_add_word(uint64_t existing, uint64_t left, uint64_t right, uint64_t carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(left, right, &high);
  unsigned __int64 sum = 0;
  unsigned char carry_out = _addcarry_u64(0, low, existing, &sum);
  high += carry_out;
  carry_out = _addcarry_u64(0, sum, carry, &sum);
  high += carry_out;
  *out = (uint64_t)sum;
  return (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)left * (__uint128_t)right + (__uint128_t)existing + (__uint128_t)carry;
  *out = (uint64_t)product;
  return (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
#endif
}

static uint64_t sub_mul_word_inplace(
  uint64_t *target,
  const uint64_t *right,
  size_t count,
  uint64_t multiplier,
  unsigned char *borrow_out) {
  uint64_t carry = 0;
  unsigned char borrow = 0;
  size_t index = 0;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
#define XRAY_SUBMUL_WORD_STEP(offset) do { \
    unsigned __int64 high = 0; \
    unsigned __int64 low = _umul128(right[index + (offset)], multiplier, &high); \
    unsigned __int64 product_low = 0; \
    unsigned char carry_out = _addcarry_u64(0, low, carry, &product_low); \
    high += carry_out; \
    unsigned __int64 diff = 0; \
    borrow = _subborrow_u64(borrow, target[index + (offset)], product_low, &diff); \
    target[index + (offset)] = (uint64_t)diff; \
    carry = (uint64_t)high; \
  } while (0)
  for (; index + 4U <= count; index += 4U) {
    XRAY_SUBMUL_WORD_STEP(0U);
    XRAY_SUBMUL_WORD_STEP(1U);
    XRAY_SUBMUL_WORD_STEP(2U);
    XRAY_SUBMUL_WORD_STEP(3U);
  }
  for (; index < count; ++index) {
    XRAY_SUBMUL_WORD_STEP(0U);
  }
#undef XRAY_SUBMUL_WORD_STEP
#else
  for (; index < count; ++index) {
    uint64_t product_low = 0;
    carry = mul_add_small_word(right[index], multiplier, carry, &product_low);
    uint64_t subtrahend = product_low + (uint64_t)borrow;
    unsigned char borrow_from_add = subtrahend < product_low;
    uint64_t before = target[index];
    target[index] = before - subtrahend;
    borrow = (unsigned char)(borrow_from_add || before < subtrahend);
  }
#endif
  if (borrow_out) *borrow_out = borrow;
  return carry;
}

#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
static uint64_t mul_add_word_unroll4_row(uint64_t *target, const uint64_t *right, uint64_t left, size_t count) {
  if (left == 0 || count == 0) return 0;
  unsigned __int64 carry = 0;
  size_t index = 0;
#define XRAY_MULADD_UNROLL4_STEP(offset) do { \
    unsigned __int64 high = 0; \
    unsigned __int64 low = _umul128(left, right[index + (offset)], &high); \
    unsigned __int64 sum = 0; \
    unsigned char carry_out = _addcarry_u64(0, low, target[index + (offset)], &sum); \
    high += carry_out; \
    carry_out = _addcarry_u64(0, sum, carry, &sum); \
    high += carry_out; \
    target[index + (offset)] = (uint64_t)sum; \
    carry = high; \
  } while (0)
  for (; index + 4U <= count; index += 4U) {
    XRAY_MULADD_UNROLL4_STEP(0U);
    XRAY_MULADD_UNROLL4_STEP(1U);
    XRAY_MULADD_UNROLL4_STEP(2U);
    XRAY_MULADD_UNROLL4_STEP(3U);
  }
  for (; index < count; ++index) {
    XRAY_MULADD_UNROLL4_STEP(0U);
  }
#undef XRAY_MULADD_UNROLL4_STEP
  return (uint64_t)carry;
}
#endif

static uint64_t mul_high_u64(uint64_t left, uint64_t right) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  (void)_umul128(left, right, &high);
  return (uint64_t)high;
#else
  return (uint64_t)(((__uint128_t)left * (__uint128_t)right) >> 64);
#endif
}

static unsigned int clz_u64(uint64_t value) {
  if (value == 0) return 64U;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned long index = 0;
  _BitScanReverse64(&index, value);
  return 63U - (unsigned int)index;
#elif defined(__GNUC__) || defined(__clang__)
  return (unsigned int)__builtin_clzll(value);
#else
  unsigned int count = 0;
  uint64_t bit = UINT64_C(1) << 63U;
  while ((value & bit) == 0) {
    count++;
    bit >>= 1U;
  }
  return count;
#endif
}

static unsigned char add_with_carry_u64(uint64_t left, uint64_t right, unsigned char carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 word = 0;
  unsigned char next = _addcarry_u64(carry, left, right, &word);
  *out = (uint64_t)word;
  return next;
#else
  uint64_t sum = left + right;
  uint64_t carry_from_sum = sum < left;
  uint64_t with_carry = sum + (uint64_t)carry;
  *out = with_carry;
  return (unsigned char)(carry_from_sum || (with_carry < sum));
#endif
}

#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
static unsigned char add_limbs_u64_unroll4(
  uint64_t *out,
  const uint64_t *left,
  const uint64_t *right,
  size_t count,
  unsigned char carry) {
  size_t index = 0;
#define XRAY_ADD_LIMBS_UNROLL4_STEP(offset) do { \
    unsigned __int64 word = 0; \
    carry = _addcarry_u64(carry, left[index + (offset)], right[index + (offset)], &word); \
    out[index + (offset)] = (uint64_t)word; \
  } while (0)
  for (; index + 4U <= count; index += 4U) {
    XRAY_ADD_LIMBS_UNROLL4_STEP(0U);
    XRAY_ADD_LIMBS_UNROLL4_STEP(1U);
    XRAY_ADD_LIMBS_UNROLL4_STEP(2U);
    XRAY_ADD_LIMBS_UNROLL4_STEP(3U);
  }
  for (; index < count; ++index) {
    XRAY_ADD_LIMBS_UNROLL4_STEP(0U);
  }
#undef XRAY_ADD_LIMBS_UNROLL4_STEP
  return carry;
}
#endif

static unsigned char sub_with_borrow_u64(uint64_t left, uint64_t right, unsigned char borrow, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 word = 0;
  unsigned char next = _subborrow_u64(borrow, left, right, &word);
  *out = (uint64_t)word;
  return next;
#else
  uint64_t subtrahend = right + (uint64_t)borrow;
  uint64_t borrow_from_add = subtrahend < right;
  *out = left - subtrahend;
  return (unsigned char)(borrow_from_add || (left < subtrahend));
#endif
}

static uint64_t reciprocal_u32(uint32_t divisor) {
  if (divisor == 1U) return 0;
  uint64_t reciprocal = UINT64_MAX / divisor;
  if (UINT64_MAX % divisor == (uint64_t)divisor - 1ULL) reciprocal++;
  return reciprocal;
}

static uint32_t divmod_half_u32(uint32_t high, uint32_t low, uint32_t divisor, uint64_t reciprocal, uint32_t *remainder) {
  if (divisor == 1U) {
    if (remainder) *remainder = 0;
    return low;
  }
  uint64_t numerator = ((uint64_t)high << 32U) | low;
  uint64_t quotient = mul_high_u64(numerator, reciprocal);
  uint64_t rem = numerator - quotient * divisor;
  while (rem >= divisor) {
    rem -= divisor;
    quotient++;
  }
  if (remainder) *remainder = (uint32_t)rem;
  return (uint32_t)quotient;
}

static uint64_t divmod_word_u32(uint32_t high, uint64_t low, uint32_t divisor, uint64_t reciprocal, int use_high_half, uint32_t *remainder) {
  uint32_t quotient_high = 0;
  uint32_t rem = high;
  if (use_high_half) {
    quotient_high = divmod_half_u32(high, (uint32_t)(low >> 32U), divisor, reciprocal, &rem);
  }
  uint32_t quotient_low = divmod_half_u32(rem, (uint32_t)low, divisor, reciprocal, &rem);
  if (remainder) *remainder = rem;
  return ((uint64_t)quotient_high << 32U) | quotient_low;
}

static uint64_t divmod_word_u32_direct(uint32_t high, uint64_t low, uint32_t divisor, uint32_t *remainder) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 rem = 0;
  unsigned __int64 quotient = _udiv128(
    (unsigned __int64)high,
    (unsigned __int64)low,
    (unsigned __int64)divisor,
    &rem);
  if (remainder) *remainder = (uint32_t)rem;
  return (uint64_t)quotient;
#else
  __uint128_t numerator = ((__uint128_t)high << XRAY_BIGINT_WORD_BITS) | (__uint128_t)low;
  if (remainder) *remainder = (uint32_t)(numerator % divisor);
  return (uint64_t)(numerator / divisor);
#endif
}

static uint64_t divmod_word_u64_direct(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 rem = 0;
  unsigned __int64 quotient = _udiv128(
    (unsigned __int64)high,
    (unsigned __int64)low,
    (unsigned __int64)divisor,
    &rem);
  if (remainder) *remainder = (uint64_t)rem;
  return (uint64_t)quotient;
#else
  __uint128_t numerator = ((__uint128_t)high << XRAY_BIGINT_WORD_BITS) | (__uint128_t)low;
  if (remainder) *remainder = (uint64_t)(numerator % divisor);
  return (uint64_t)(numerator / divisor);
#endif
}

static uint64_t invert_limb_u64(uint64_t divisor) {
  return divmod_word_u64_direct(UINT64_MAX - divisor, UINT64_MAX, divisor, NULL);
}

static uint64_t divmod_word_u64_preinv(
  uint64_t high,
  uint64_t low,
  uint64_t divisor,
  uint64_t inverse,
  uint64_t *remainder) {
  uint64_t qhat = high + mul_high_u64(high, inverse);
  uint64_t product_low = 0;
  uint64_t product_high = 0;

  for (;;) {
    product_high = mul_add_small_word(qhat, divisor, 0, &product_low);
    if (product_high < high || (product_high == high && product_low <= low)) break;
    qhat--;
  }

  uint64_t borrow = low < product_low ? 1U : 0U;
  uint64_t rem_low = low - product_low;
  uint64_t rem_high = high - product_high - borrow;
  while (rem_high || rem_low >= divisor) {
    uint64_t old_low = rem_low;
    rem_low -= divisor;
    if (old_low < divisor) rem_high--;
    qhat++;
  }

  if (remainder) *remainder = rem_low;
  return qhat;
}

static uint32_t divmod_decimal_chunk_inplace(XrayScratchBigInt *value) {
  uint32_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    value->limbs[index] = divmod_word_u32(
      remainder,
      value->limbs[index],
      XRAY_BIGINT_DECIMAL_CHUNK_BASE,
      XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
      use_high_half,
      &remainder);
  }
  normalize(value);
  return remainder;
}

static int reserve_decimal_chunks(uint32_t **chunks, size_t *capacity, size_t needed) {
  if (*capacity >= needed) return 1;
  if (needed > SIZE_MAX / sizeof(uint32_t)) return 0;
  size_t next_capacity = *capacity ? *capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(uint32_t)) return 0;
  uint32_t *next = (uint32_t *)realloc(*chunks, sizeof(uint32_t) * next_capacity);
  if (!next) return 0;
  *chunks = next;
  *capacity = next_capacity;
  return 1;
}

static size_t estimate_decimal_chunk_capacity(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  if (value->count > (SIZE_MAX - 8U) / 20U) return SIZE_MAX;
  return (value->count * 20U + 8U) / XRAY_BIGINT_DECIMAL_CHUNK_DIGITS;
}

static int append_decimal_chunk(uint32_t **chunks, size_t *count, size_t *capacity, uint32_t chunk) {
  if (!reserve_decimal_chunks(chunks, capacity, *count + 1U)) return 0;
  (*chunks)[(*count)++] = chunk;
  return 1;
}

static int reserve_decimal_wide_chunks(uint64_t **chunks, size_t *capacity, size_t needed) {
  if (*capacity >= needed) return 1;
  if (needed > SIZE_MAX / sizeof(uint64_t)) return 0;
  size_t next_capacity = *capacity ? *capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(uint64_t)) return 0;
  uint64_t *next = (uint64_t *)realloc(*chunks, sizeof(uint64_t) * next_capacity);
  if (!next) return 0;
  *chunks = next;
  *capacity = next_capacity;
  return 1;
}

static size_t estimate_decimal_wide_chunk_capacity(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  if (value->count > (SIZE_MAX - 8U) / 20U) return SIZE_MAX;
  return (value->count * 20U + 8U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
}

static int append_decimal_wide_chunk(uint64_t **chunks, size_t *count, size_t *capacity, uint64_t chunk) {
  if (!reserve_decimal_wide_chunks(chunks, capacity, *count + 1U)) return 0;
  (*chunks)[(*count)++] = chunk;
  return 1;
}

static int mul_add_small_inplace(XrayScratchBigInt *value, uint64_t multiplier, uint64_t addend);
static size_t write_u32_decimal_pairs(char *out, uint32_t value);
static void write_u32_decimal_padded9_mixed_pairs(char *out, uint32_t value);
static size_t write_u64_decimal(char *out, uint64_t value);
static size_t write_u64_decimal_pairs(char *out, uint64_t value);
static void write_u64_decimal_padded19(char *out, uint64_t value);
static void write_u64_decimal_padded19_pairs(char *out, uint64_t value);

static int decimal_chunks_from_limbs_horner(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t remainder = 0;
      carry = divmod_word_u32(
        chunks[index],
        carry,
        XRAY_BIGINT_DECIMAL_CHUNK_BASE,
        XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
        1,
        &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = divmod_word_u32(
        0,
        carry,
        XRAY_BIGINT_DECIMAL_CHUNK_BASE,
        XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
        (carry >> 32U) != 0,
        &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint64_t divmod_u64_decimal_chunk(uint64_t value, uint32_t *remainder) {
  return divmod_word_u32(
    0,
    value,
    XRAY_BIGINT_DECIMAL_CHUNK_BASE,
    XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
    (value >> 32U) != 0,
    remainder);
}

static uint64_t divmod_u64_decimal_chunk_direct(uint64_t value, uint32_t *remainder) {
  return divmod_word_u32_direct(0, value, XRAY_BIGINT_DECIMAL_CHUNK_BASE, remainder);
}

static uint64_t divmod_folded_decimal_chunk(uint64_t low, int overflow, uint32_t *remainder) {
  if (!overflow) return divmod_u64_decimal_chunk(low, remainder);
  uint64_t adjusted = low + (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
  return XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + divmod_u64_decimal_chunk(adjusted, remainder);
}

static uint64_t divmod_folded_decimal_chunk_direct(uint64_t low, int overflow, uint32_t *remainder) {
  if (!overflow) return divmod_u64_decimal_chunk_direct(low, remainder);
  uint64_t adjusted = low + (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
  return XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + divmod_u64_decimal_chunk_direct(adjusted, remainder);
}

static int decimal_chunks_from_limbs_horner_folded_mode(
  uint32_t **chunks_out,
  size_t *chunk_count_out,
  const XrayScratchBigInt *value,
  int use_direct_divider) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t chunk = chunks[index];
      uint64_t scaled = (uint64_t)chunk * (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
      uint64_t low = scaled + carry;
      uint32_t remainder = 0;
      uint64_t carry_delta = use_direct_divider ?
        divmod_folded_decimal_chunk_direct(low, low < scaled, &remainder) :
        divmod_folded_decimal_chunk(low, low < scaled, &remainder);
      chunks[index] = remainder;
      carry = (uint64_t)chunk * XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + carry_delta;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = use_direct_divider ?
        divmod_u64_decimal_chunk_direct(carry, &remainder) :
        divmod_u64_decimal_chunk(carry, &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static int decimal_chunks_from_limbs_horner_folded(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  return decimal_chunks_from_limbs_horner_folded_mode(chunks_out, chunk_count_out, value, 0);
}

static int decimal_chunks_from_limbs_horner_folded_direct(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  return decimal_chunks_from_limbs_horner_folded_mode(chunks_out, chunk_count_out, value, 1);
}

static int decimal_wide_chunks_from_limbs_horner(uint64_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_wide_chunks(&chunks, &chunk_capacity, estimate_decimal_wide_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint64_t remainder = 0;
      carry = divmod_word_u64_direct(
        chunks[index],
        carry,
        XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
        &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint64_t remainder = 0;
      carry = divmod_word_u64_direct(
        0,
        carry,
        XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
        &remainder);
      if (!append_decimal_wide_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint64_t divmod_decimal_wide_chunk_inplace(XrayScratchBigInt *value) {
  uint64_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    value->limbs[index] = divmod_word_u64_direct(
      remainder,
      value->limbs[index],
      XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
      &remainder);
  }
  normalize(value);
  return remainder;
}

static uint64_t divmod_decimal_wide_chunk_preinv_inplace(XrayScratchBigInt *value) {
  uint64_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    value->limbs[index] = divmod_word_u64_preinv(
      remainder,
      value->limbs[index],
      XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
      XRAY_BIGINT_DECIMAL_WIDE_CHUNK_PREINVERSE,
      &remainder);
  }
  normalize(value);
  return remainder;
}

static int decimal_wide_chunks_from_limbs_divide_mode(
  uint64_t **chunks_out,
  size_t *chunk_count_out,
  const XrayScratchBigInt *value,
  int use_preinv) {
  XrayScratchBigInt copy;
  xray_bigint_init(&copy);
  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  int ok = xray_bigint_copy(&copy, value) &&
    reserve_decimal_wide_chunks(&chunks, &chunk_capacity, estimate_decimal_wide_chunk_capacity(value));
  while (ok && copy.count > 0) {
    ok = append_decimal_wide_chunk(
      &chunks,
      &chunk_count,
      &chunk_capacity,
      use_preinv ?
        divmod_decimal_wide_chunk_preinv_inplace(&copy) :
        divmod_decimal_wide_chunk_inplace(&copy));
  }
  xray_bigint_clear(&copy);
  if (!ok) {
    free(chunks);
    return 0;
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static int decimal_wide_chunks_from_limbs_divide(uint64_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  return decimal_wide_chunks_from_limbs_divide_mode(chunks_out, chunk_count_out, value, 0);
}

static int shift_left_bits_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value, unsigned int shift) {
  if (!out || !value || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (shift == 0) return xray_bigint_copy(out, value);
  if (value->count == 0) return set_u32(out, 0);
  if (!reserve_limbs(out, value->count + 1U)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    out->limbs[index] = (word << shift) | carry;
    carry = word >> (XRAY_BIGINT_WORD_BITS - shift);
  }
  out->count = value->count;
  if (carry) out->limbs[out->count++] = carry;
  normalize(out);
  return 1;
}

static int shift_right_bits_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value, unsigned int shift) {
  if (!out || !value || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (shift == 0) return xray_bigint_copy(out, value);
  if (value->count == 0) return set_u32(out, 0);
  if (!reserve_limbs(out, value->count)) return 0;
  uint64_t carry = 0;
  uint64_t low_mask = (UINT64_C(1) << shift) - 1U;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1U;
    uint64_t word = value->limbs[index];
    out->limbs[index] = (word >> shift) | (carry << (XRAY_BIGINT_WORD_BITS - shift));
    carry = word & low_mask;
  }
  out->count = value->count;
  normalize(out);
  return 1;
}

static int divmod_bigint_u64_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder_out,
  const XrayScratchBigInt *value,
  uint64_t divisor) {
  if (!quotient || !remainder_out || !value || divisor == 0) return 0;
  if (value->count == 0) {
    quotient->count = 0;
    remainder_out->count = 0;
    return 1;
  }
  if (!reserve_limbs(quotient, value->count)) return 0;
  uint64_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1U;
    quotient->limbs[index] = divmod_word_u64_direct(remainder, value->limbs[index], divisor, &remainder);
  }
  quotient->count = value->count;
  normalize(quotient);
  return set_u64(remainder_out, remainder);
}

static int product_gt_two_limb(uint64_t left, uint64_t right, uint64_t high, uint64_t low) {
  uint64_t product_low = 0;
  uint64_t product_high = mul_add_small_word(left, right, 0, &product_low);
  return product_high > high || (product_high == high && product_low > low);
}

static int divmod_bigint_normalized_workspace_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  XrayBigIntDivisionWorkspace *workspace);

static int divmod_bigint_normalized_preinv_workspace_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  XrayBigIntDivisionWorkspace *workspace);

static int divmod_bigint_normalized_preinv_workspace_with_inverse_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  uint64_t normalized_top_inverse,
  XrayBigIntDivisionWorkspace *workspace);

static int divmod_bigint_normalized_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift) {
  XrayBigIntDivisionWorkspace workspace;
  xray_bigint_division_workspace_init(&workspace);
  int ok = divmod_bigint_normalized_workspace_probe(
    quotient,
    remainder,
    numerator,
    normalized_divisor,
    shift,
    &workspace);
  xray_bigint_division_workspace_clear(&workspace);
  return ok;
}

static int divmod_bigint_normalized_workspace_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!quotient || !remainder || !numerator || !normalized_divisor || !workspace || normalized_divisor->count == 0) return 0;
  size_t n = normalized_divisor->count;
  size_t m = numerator->count - n;
  XrayScratchBigInt *normalized_numerator = &workspace->normalized_numerator;
  XrayScratchBigInt *remainder_slice = &workspace->remainder_slice;

  int ok = shift_left_bits_copy(normalized_numerator, numerator, shift) &&
    reserve_limbs(normalized_numerator, n + m + 1U) &&
    reserve_limbs(quotient, m + 1U);
  if (ok) {
    while (normalized_numerator->count < n + m + 1U) {
      normalized_numerator->limbs[normalized_numerator->count++] = 0;
    }
    memset(quotient->limbs, 0, sizeof(uint64_t) * (m + 1U));
    quotient->count = m + 1U;

    for (size_t jj = m + 1U; jj > 0; --jj) {
      size_t j = jj - 1U;
      uint64_t qhat = 0;
      uint64_t rhat = 0;
      uint64_t ujn = normalized_numerator->limbs[j + n];
      uint64_t ujn1 = normalized_numerator->limbs[j + n - 1U];
      uint64_t vn1 = normalized_divisor->limbs[n - 1U];
      int rhat_overflow = 0;
      if (ujn == vn1) {
        qhat = UINT64_MAX;
        rhat = ujn1 + vn1;
        rhat_overflow = rhat < ujn1;
      } else {
        qhat = divmod_word_u64_direct(ujn, ujn1, vn1, &rhat);
      }

      if (n > 1U) {
        uint64_t vn2 = normalized_divisor->limbs[n - 2U];
        uint64_t ujn2 = normalized_numerator->limbs[j + n - 2U];
        while (!rhat_overflow && product_gt_two_limb(qhat, vn2, rhat, ujn2)) {
          qhat--;
          uint64_t old_rhat = rhat;
          rhat += vn1;
          rhat_overflow = rhat < old_rhat;
        }
      }

      unsigned char borrow = 0;
      uint64_t carry = sub_mul_word_inplace(
        &normalized_numerator->limbs[j],
        normalized_divisor->limbs,
        n,
        qhat,
        &borrow);
      borrow = sub_with_borrow_u64(
        normalized_numerator->limbs[j + n],
        carry,
        borrow,
        &normalized_numerator->limbs[j + n]);

      if (borrow) {
        qhat--;
        unsigned char carry_back = 0;
        for (size_t index = 0; index < n; ++index) {
          carry_back = add_with_carry_u64(
            normalized_numerator->limbs[j + index],
            normalized_divisor->limbs[index],
            carry_back,
            &normalized_numerator->limbs[j + index]);
        }
        add_with_carry_u64(
          normalized_numerator->limbs[j + n],
          0,
          carry_back,
          &normalized_numerator->limbs[j + n]);
      }
      quotient->limbs[j] = qhat;
    }

    normalize(quotient);
    ok = reserve_limbs(remainder_slice, n) != 0;
    if (ok) {
      memcpy(remainder_slice->limbs, normalized_numerator->limbs, sizeof(uint64_t) * n);
      remainder_slice->count = n;
      normalize(remainder_slice);
      ok = shift_right_bits_copy(remainder, remainder_slice, shift);
    }
  }

  return ok;
}

static int divmod_bigint_normalized_preinv_workspace_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!normalized_divisor || normalized_divisor->count == 0) return 0;
  uint64_t vn1 = normalized_divisor->limbs[normalized_divisor->count - 1U];
  return divmod_bigint_normalized_preinv_workspace_with_inverse_probe(
    quotient,
    remainder,
    numerator,
    normalized_divisor,
    shift,
    invert_limb_u64(vn1),
    workspace);
}

static int divmod_bigint_normalized_preinv_workspace_with_inverse_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift,
  uint64_t normalized_top_inverse,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!quotient || !remainder || !numerator || !normalized_divisor || !workspace || normalized_divisor->count == 0) return 0;
  size_t n = normalized_divisor->count;
  size_t m = numerator->count - n;
  XrayScratchBigInt *normalized_numerator = &workspace->normalized_numerator;
  XrayScratchBigInt *remainder_slice = &workspace->remainder_slice;
  uint64_t vn1 = normalized_divisor->limbs[n - 1U];

  int ok = shift_left_bits_copy(normalized_numerator, numerator, shift) &&
    reserve_limbs(normalized_numerator, n + m + 1U) &&
    reserve_limbs(quotient, m + 1U);
  if (ok) {
    while (normalized_numerator->count < n + m + 1U) {
      normalized_numerator->limbs[normalized_numerator->count++] = 0;
    }
    memset(quotient->limbs, 0, sizeof(uint64_t) * (m + 1U));
    quotient->count = m + 1U;

    for (size_t jj = m + 1U; jj > 0; --jj) {
      size_t j = jj - 1U;
      uint64_t qhat = 0;
      uint64_t rhat = 0;
      uint64_t ujn = normalized_numerator->limbs[j + n];
      uint64_t ujn1 = normalized_numerator->limbs[j + n - 1U];
      int rhat_overflow = 0;
      if (ujn == vn1) {
        qhat = UINT64_MAX;
        rhat = ujn1 + vn1;
        rhat_overflow = rhat < ujn1;
      } else {
        qhat = divmod_word_u64_preinv(ujn, ujn1, vn1, normalized_top_inverse, &rhat);
      }

      if (n > 1U) {
        uint64_t vn2 = normalized_divisor->limbs[n - 2U];
        uint64_t ujn2 = normalized_numerator->limbs[j + n - 2U];
        while (!rhat_overflow && product_gt_two_limb(qhat, vn2, rhat, ujn2)) {
          qhat--;
          uint64_t old_rhat = rhat;
          rhat += vn1;
          rhat_overflow = rhat < old_rhat;
        }
      }

      unsigned char borrow = 0;
      uint64_t carry = sub_mul_word_inplace(
        &normalized_numerator->limbs[j],
        normalized_divisor->limbs,
        n,
        qhat,
        &borrow);
      borrow = sub_with_borrow_u64(
        normalized_numerator->limbs[j + n],
        carry,
        borrow,
        &normalized_numerator->limbs[j + n]);

      if (borrow) {
        qhat--;
        unsigned char carry_back = 0;
        for (size_t index = 0; index < n; ++index) {
          carry_back = add_with_carry_u64(
            normalized_numerator->limbs[j + index],
            normalized_divisor->limbs[index],
            carry_back,
            &normalized_numerator->limbs[j + index]);
        }
        add_with_carry_u64(
          normalized_numerator->limbs[j + n],
          0,
          carry_back,
          &normalized_numerator->limbs[j + n]);
      }
      quotient->limbs[j] = qhat;
    }

    normalize(quotient);
    ok = reserve_limbs(remainder_slice, n) != 0;
    if (ok) {
      memcpy(remainder_slice->limbs, normalized_numerator->limbs, sizeof(uint64_t) * n);
      remainder_slice->count = n;
      normalize(remainder_slice);
      ok = shift_right_bits_copy(remainder, remainder_slice, shift);
    }
  }

  return ok;
}

static int divmod_bigint_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *divisor) {
  if (!quotient || !remainder || !numerator || !divisor || divisor->count == 0) return 0;
  int ordering = xray_bigint_compare(numerator, divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (divisor->count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, divisor->limbs[0]);
  }

  size_t n = divisor->count;
  unsigned int shift = clz_u64(divisor->limbs[n - 1U]);
  XrayScratchBigInt normalized_divisor;
  xray_bigint_init(&normalized_divisor);

  int ok = shift_left_bits_copy(&normalized_divisor, divisor, shift) &&
    divmod_bigint_normalized_probe(quotient, remainder, numerator, &normalized_divisor, shift);

  xray_bigint_clear(&normalized_divisor);
  return ok;
}

void xray_bigint_divisor_context_init(XrayBigIntDivisorContext *context) {
  if (!context) return;
  xray_bigint_init(&context->divisor);
  xray_bigint_init(&context->normalized_divisor);
  context->normalization_shift = 0;
  context->valid = 0;
}

void xray_bigint_divisor_context_clear(XrayBigIntDivisorContext *context) {
  if (!context) return;
  xray_bigint_clear(&context->divisor);
  xray_bigint_clear(&context->normalized_divisor);
  context->normalization_shift = 0;
  context->valid = 0;
}

void xray_bigint_division_workspace_init(XrayBigIntDivisionWorkspace *workspace) {
  if (!workspace) return;
  xray_bigint_init(&workspace->normalized_numerator);
  xray_bigint_init(&workspace->remainder_slice);
}

void xray_bigint_division_workspace_clear(XrayBigIntDivisionWorkspace *workspace) {
  if (!workspace) return;
  xray_bigint_clear(&workspace->normalized_numerator);
  xray_bigint_clear(&workspace->remainder_slice);
}

int xray_bigint_divisor_context_set(XrayBigIntDivisorContext *context, const XrayScratchBigInt *divisor) {
  if (!context) return 0;
  context->valid = 0;
  context->normalization_shift = 0;
  if (!divisor || divisor->count == 0) {
    context->divisor.count = 0;
    context->normalized_divisor.count = 0;
    return 0;
  }
  unsigned int shift = clz_u64(divisor->limbs[divisor->count - 1U]);
  int ok = xray_bigint_copy(&context->divisor, divisor) &&
    shift_left_bits_copy(&context->normalized_divisor, divisor, shift);
  if (!ok) {
    context->divisor.count = 0;
    context->normalized_divisor.count = 0;
    return 0;
  }
  context->normalization_shift = shift;
  context->valid = 1;
  return 1;
}

static int divmod_bigint_precomputed_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context) {
  if (!quotient || !remainder || !numerator || !context || !context->valid || context->divisor.count == 0) return 0;
  int ordering = xray_bigint_compare(numerator, &context->divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (context->divisor.count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, context->divisor.limbs[0]);
  }
  return divmod_bigint_normalized_probe(
    quotient,
    remainder,
    numerator,
    &context->normalized_divisor,
    context->normalization_shift);
}

static int divmod_bigint_precomputed_workspace_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!quotient || !remainder || !numerator || !context || !context->valid || context->divisor.count == 0 || !workspace) return 0;
  int ordering = xray_bigint_compare(numerator, &context->divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (context->divisor.count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, context->divisor.limbs[0]);
  }
  return divmod_bigint_normalized_workspace_probe(
    quotient,
    remainder,
    numerator,
    &context->normalized_divisor,
    context->normalization_shift,
    workspace);
}

int xray_bigint_divmod(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *divisor) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !divisor || divisor->count == 0) return 0;
  if (quotient == numerator || quotient == divisor || remainder == numerator || remainder == divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = divmod_bigint_probe(&quotient_temp, &remainder_temp, numerator, divisor);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  return divmod_bigint_probe(quotient, remainder, numerator, divisor);
}

int xray_bigint_divmod_precomputed(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !context || !context->valid || context->divisor.count == 0) return 0;
  if (quotient == numerator ||
      remainder == numerator ||
      quotient == &context->divisor ||
      quotient == &context->normalized_divisor ||
      remainder == &context->divisor ||
      remainder == &context->normalized_divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = divmod_bigint_precomputed_probe(&quotient_temp, &remainder_temp, numerator, context);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  return divmod_bigint_precomputed_probe(quotient, remainder, numerator, context);
}

int xray_bigint_divmod_precomputed_workspace(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !context || !context->valid || context->divisor.count == 0 || !workspace) return 0;
  if (numerator == &workspace->normalized_numerator ||
      numerator == &workspace->remainder_slice ||
      quotient == &workspace->normalized_numerator ||
      quotient == &workspace->remainder_slice ||
      remainder == &workspace->normalized_numerator ||
      remainder == &workspace->remainder_slice) {
    return 0;
  }
  if (quotient == numerator ||
      remainder == numerator ||
      quotient == &context->divisor ||
      quotient == &context->normalized_divisor ||
      remainder == &context->divisor ||
      remainder == &context->normalized_divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = divmod_bigint_precomputed_workspace_probe(&quotient_temp, &remainder_temp, numerator, context, workspace);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  return divmod_bigint_precomputed_workspace_probe(quotient, remainder, numerator, context, workspace);
}

int xray_bigint_divmod_preinv_qhat_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context,
  XrayBigIntDivisionWorkspace *workspace) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !context || !context->valid || context->divisor.count == 0 || !workspace) return 0;
  if (numerator == &workspace->normalized_numerator ||
      numerator == &workspace->remainder_slice ||
      quotient == &workspace->normalized_numerator ||
      quotient == &workspace->remainder_slice ||
      remainder == &workspace->normalized_numerator ||
      remainder == &workspace->remainder_slice) {
    return 0;
  }
  if (quotient == numerator ||
      remainder == numerator ||
      quotient == &context->divisor ||
      quotient == &context->normalized_divisor ||
      remainder == &context->divisor ||
      remainder == &context->normalized_divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = xray_bigint_divmod_preinv_qhat_probe(&quotient_temp, &remainder_temp, numerator, context, workspace);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  int ordering = xray_bigint_compare(numerator, &context->divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (context->divisor.count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, context->divisor.limbs[0]);
  }
  return divmod_bigint_normalized_preinv_workspace_probe(
    quotient,
    remainder,
    numerator,
    &context->normalized_divisor,
    context->normalization_shift,
    workspace);
}

typedef struct {
  size_t chunks;
  XrayScratchBigInt value;
} XrayDecimalDcPower;

static XrayDecimalDcPower decimal_dc_static_split_values[] = {
  {108U, {(uint64_t *)decimal_dc_static_split_108, 107U, 107U}},
  {216U, {(uint64_t *)decimal_dc_static_split_216, 214U, 214U}},
};

typedef struct {
  XrayDecimalDcPower *items;
  size_t count;
  size_t capacity;
  XrayScratchBigInt *ladder;
  size_t ladder_count;
  size_t ladder_capacity;
  int use_ladder;
  int use_static_ladder;
} XrayDecimalDcPowerCache;

typedef struct {
  size_t chunks;
  XrayBigIntDivisorContext context;
  uint64_t normalized_top_inverse;
} XrayDecimalDcContextEntry;

typedef struct {
  XrayDecimalDcContextEntry *items;
  size_t count;
  size_t capacity;
} XrayDecimalDcContextCache;

typedef enum {
  XRAY_DECIMAL_DC_DIVMOD_DEFAULT = 0,
  XRAY_DECIMAL_DC_DIVMOD_WORKSPACE = 1,
  XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT = 2
} XrayDecimalDcDivmodMode;

static const XrayScratchBigInt *decimal_dc_static_ladder_get(size_t bit_index) {
  size_t count = sizeof(decimal_dc_static_ladder_values) / sizeof(decimal_dc_static_ladder_values[0]);
  return bit_index < count ? &decimal_dc_static_ladder_values[bit_index] : NULL;
}

static const XrayScratchBigInt *decimal_dc_static_split_get(size_t chunks) {
  size_t count = sizeof(decimal_dc_static_split_values) / sizeof(decimal_dc_static_split_values[0]);
  for (size_t index = 0; index < count; ++index) {
    if (decimal_dc_static_split_values[index].chunks == chunks) {
      return &decimal_dc_static_split_values[index].value;
    }
  }
  return NULL;
}

static void decimal_dc_power_cache_init(XrayDecimalDcPowerCache *cache, int use_ladder, int use_static_ladder) {
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
  cache->ladder = NULL;
  cache->ladder_count = 0;
  cache->ladder_capacity = 0;
  cache->use_ladder = use_ladder;
  cache->use_static_ladder = use_static_ladder;
}

static void decimal_dc_power_cache_clear(XrayDecimalDcPowerCache *cache) {
  if (!cache) return;
  for (size_t index = 0; index < cache->count; ++index) {
    xray_bigint_clear(&cache->items[index].value);
  }
  for (size_t index = 0; index < cache->ladder_count; ++index) {
    xray_bigint_clear(&cache->ladder[index]);
  }
  free(cache->items);
  free(cache->ladder);
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
  cache->ladder = NULL;
  cache->ladder_count = 0;
  cache->ladder_capacity = 0;
}

static void decimal_dc_context_cache_init(XrayDecimalDcContextCache *cache) {
  if (!cache) return;
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
}

static void decimal_dc_context_cache_clear(XrayDecimalDcContextCache *cache) {
  if (!cache) return;
  for (size_t index = 0; index < cache->count; ++index) {
    xray_bigint_divisor_context_clear(&cache->items[index].context);
  }
  free(cache->items);
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
}

static int decimal_dc_context_cache_reserve(XrayDecimalDcContextCache *cache, size_t needed) {
  if (cache->capacity >= needed) return 1;
  size_t old_capacity = cache->capacity;
  size_t next_capacity = cache->capacity ? cache->capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(XrayDecimalDcContextEntry)) return 0;
  XrayDecimalDcContextEntry *next =
    (XrayDecimalDcContextEntry *)realloc(cache->items, sizeof(XrayDecimalDcContextEntry) * next_capacity);
  if (!next) return 0;
  cache->items = next;
  for (size_t index = old_capacity; index < next_capacity; ++index) {
    cache->items[index].chunks = 0;
    cache->items[index].normalized_top_inverse = 0;
    xray_bigint_divisor_context_init(&cache->items[index].context);
  }
  cache->capacity = next_capacity;
  return 1;
}

static int decimal_dc_power_cache_reserve(XrayDecimalDcPowerCache *cache, size_t needed) {
  if (cache->capacity >= needed) return 1;
  size_t next_capacity = cache->capacity ? cache->capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(XrayDecimalDcPower)) return 0;
  XrayDecimalDcPower *next = (XrayDecimalDcPower *)realloc(cache->items, sizeof(XrayDecimalDcPower) * next_capacity);
  if (!next) return 0;
  cache->items = next;
  cache->capacity = next_capacity;
  return 1;
}

static int decimal_dc_power_ladder_reserve(XrayDecimalDcPowerCache *cache, size_t needed) {
  if (cache->ladder_capacity >= needed) return 1;
  size_t old_capacity = cache->ladder_capacity;
  size_t next_capacity = cache->ladder_capacity ? cache->ladder_capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(XrayScratchBigInt)) return 0;
  XrayScratchBigInt *next = (XrayScratchBigInt *)realloc(cache->ladder, sizeof(XrayScratchBigInt) * next_capacity);
  if (!next) return 0;
  cache->ladder = next;
  for (size_t index = old_capacity; index < next_capacity; ++index) {
    xray_bigint_init(&cache->ladder[index]);
  }
  cache->ladder_capacity = next_capacity;
  return 1;
}

static const XrayScratchBigInt *decimal_dc_power_ladder_get(XrayDecimalDcPowerCache *cache, size_t bit_index) {
  if (!cache) return NULL;
  if (cache->use_static_ladder) {
    const XrayScratchBigInt *static_power = decimal_dc_static_ladder_get(bit_index);
    if (static_power) return static_power;
  }
  if (!decimal_dc_power_ladder_reserve(cache, bit_index + 1U)) return NULL;
  while (cache->ladder_count <= bit_index) {
    size_t target = cache->ladder_count;
    int ok = 0;
    const XrayScratchBigInt *static_power = cache->use_static_ladder ? decimal_dc_static_ladder_get(target) : NULL;
    if (static_power) {
      ok = xray_bigint_copy(&cache->ladder[target], static_power);
    } else if (target == 0) {
      ok = set_u64(&cache->ladder[target], XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE);
    } else {
      ok = xray_bigint_mul(&cache->ladder[target], &cache->ladder[target - 1U], &cache->ladder[target - 1U]);
    }
    if (!ok) return NULL;
    cache->ladder_count++;
  }
  return &cache->ladder[bit_index];
}

static int decimal_dc_power_cache_store(
  XrayDecimalDcPowerCache *cache,
  size_t chunks,
  const XrayScratchBigInt *power) {
  if (!cache || !power) return 0;
  if (!decimal_dc_power_cache_reserve(cache, cache->count + 1U)) return 0;
  size_t target = cache->count++;
  cache->items[target].chunks = chunks;
  xray_bigint_init(&cache->items[target].value);
  int ok = xray_bigint_copy(&cache->items[target].value, power);
  if (!ok) {
    xray_bigint_clear(&cache->items[target].value);
    cache->count--;
  }
  return ok;
}

static int decimal_dc_power_from_ladder(
  XrayDecimalDcPowerCache *cache,
  XrayScratchBigInt *out,
  size_t chunks) {
  if (!cache || !out) return 0;
  int ok = set_u32(out, 1U);
  size_t remaining = chunks;
  size_t bit_index = 0;
  while (ok && remaining) {
    if (remaining & 1U) {
      const XrayScratchBigInt *factor = decimal_dc_power_ladder_get(cache, bit_index);
      if (!factor) return 0;
      XrayScratchBigInt product;
      xray_bigint_init(&product);
      ok = xray_bigint_mul(&product, out, factor) && xray_bigint_copy(out, &product);
      xray_bigint_clear(&product);
    }
    remaining >>= 1U;
    bit_index++;
  }
  return ok;
}

static const XrayScratchBigInt *decimal_dc_power_cache_get(XrayDecimalDcPowerCache *cache, size_t chunks) {
  if (!cache) return NULL;
  if (cache->use_ladder) {
    const XrayScratchBigInt *static_split = decimal_dc_static_split_get(chunks);
    if (static_split) return static_split;
  }
  for (size_t index = 0; index < cache->count; ++index) {
    if (cache->items[index].chunks == chunks) return &cache->items[index].value;
  }

  size_t source_index = SIZE_MAX;
  size_t source_chunks = 0;
  for (size_t index = 0; index < cache->count; ++index) {
    if (cache->items[index].chunks <= chunks && cache->items[index].chunks >= source_chunks) {
      source_index = index;
      source_chunks = cache->items[index].chunks;
    }
  }

  XrayScratchBigInt power;
  xray_bigint_init(&power);
  int ok = 0;
  if (cache->use_ladder) {
    ok = decimal_dc_power_from_ladder(cache, &power, chunks);
  } else {
    ok = source_index == SIZE_MAX ? set_u32(&power, 1U) : xray_bigint_copy(&power, &cache->items[source_index].value);
    for (size_t index = source_chunks; ok && index < chunks; ++index) {
      ok = mul_add_small_inplace(&power, XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE, 0);
    }
  }

  if (ok) ok = decimal_dc_power_cache_store(cache, chunks, &power);
  xray_bigint_clear(&power);
  if (!ok) return NULL;
  return &cache->items[cache->count - 1U].value;
}

static const XrayDecimalDcContextEntry *decimal_dc_context_cache_get(
  XrayDecimalDcContextCache *context_cache,
  XrayDecimalDcPowerCache *power_cache,
  size_t chunks) {
  if (!context_cache || !power_cache) return NULL;
  for (size_t index = 0; index < context_cache->count; ++index) {
    if (context_cache->items[index].chunks == chunks) {
      return &context_cache->items[index];
    }
  }
  const XrayScratchBigInt *power = decimal_dc_power_cache_get(power_cache, chunks);
  if (!power) return NULL;
  if (!decimal_dc_context_cache_reserve(context_cache, context_cache->count + 1U)) return NULL;
  XrayDecimalDcContextEntry *entry = &context_cache->items[context_cache->count];
  entry->chunks = chunks;
  if (!xray_bigint_divisor_context_set(&entry->context, power)) {
    entry->chunks = 0;
    entry->normalized_top_inverse = 0;
    return NULL;
  }
  entry->normalized_top_inverse = entry->context.normalized_divisor.count > 0 ?
    invert_limb_u64(entry->context.normalized_divisor.limbs[entry->context.normalized_divisor.count - 1U]) :
    0;
  context_cache->count++;
  return entry;
}

static size_t estimate_decimal_digits_from_bits(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  unsigned int top_bits = XRAY_BIGINT_WORD_BITS - clz_u64(value->limbs[value->count - 1U]);
  size_t bits = (value->count - 1U) * XRAY_BIGINT_WORD_BITS + top_bits;
  if (bits > (SIZE_MAX - 4096U) / 1233U) {
    size_t chunks = estimate_decimal_wide_chunk_capacity(value);
    if (chunks > SIZE_MAX / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return SIZE_MAX;
    return chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  }
  return (bits * 1233U) / 4096U + 1U;
}

static size_t estimate_decimal_wide_chunks_from_bits(const XrayScratchBigInt *value) {
  size_t digits = estimate_decimal_digits_from_bits(value);
  return digits / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
}

static int decimal_dc_divmod_with_mode(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *divisor,
  XrayDecimalDcDivmodMode mode,
  XrayBigIntDivisionWorkspace *workspace) {
  if (mode == XRAY_DECIMAL_DC_DIVMOD_DEFAULT) {
    return divmod_bigint_probe(quotient, remainder, numerator, divisor);
  }
  if (!workspace) return 0;

  XrayBigIntDivisorContext context;
  xray_bigint_divisor_context_init(&context);
  int ok = xray_bigint_divisor_context_set(&context, divisor);
  if (ok && mode == XRAY_DECIMAL_DC_DIVMOD_WORKSPACE) {
    ok = xray_bigint_divmod_precomputed_workspace(quotient, remainder, numerator, &context, workspace);
  } else if (ok && mode == XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT) {
    ok = xray_bigint_divmod_preinv_qhat_probe(quotient, remainder, numerator, &context, workspace);
  } else {
    ok = 0;
  }
  xray_bigint_divisor_context_clear(&context);
  return ok;
}

static char *format_decimal_dc_internal(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *cache,
  size_t leaf_chunks,
  unsigned int depth) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (leaf_chunks == 0) leaf_chunks = 32U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= leaf_chunks || depth >= 64U) {
    return xray_bigint_get_decimal_divide_1e19_probe(value);
  }

  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(cache, split_chunks);
    if (!power) return NULL;
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) return xray_bigint_get_decimal_divide_1e19_probe(value);

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  char *left = NULL;
  char *right = NULL;
  char *combined = NULL;
  int ok = divmod_bigint_probe(&quotient, &remainder, value, power);
  if (ok && quotient.count == 0) {
    xray_bigint_clear(&quotient);
    xray_bigint_clear(&remainder);
    return xray_bigint_get_decimal_divide_1e19_probe(value);
  }
  if (ok) {
    left = format_decimal_dc_internal(&quotient, cache, leaf_chunks, depth + 1U);
    right = format_decimal_dc_internal(&remainder, cache, leaf_chunks, depth + 1U);
    ok = left != NULL && right != NULL;
  }
  if (ok) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    size_t right_width = split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
    if (right_len > right_width) ok = 0;
    if (ok && left_len > SIZE_MAX - right_width - 1U) ok = 0;
    if (ok) {
      combined = (char *)calloc(left_len + right_width + 1U, 1);
      ok = combined != NULL;
    }
    if (ok) {
      memcpy(combined, left, left_len);
      memset(combined + left_len, '0', right_width - right_len);
      memcpy(combined + left_len + right_width - right_len, right, right_len);
    }
  }
  free(left);
  free(right);
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok ? combined : NULL;
}

static int write_decimal_wide_chunks_tail(
  char *buffer,
  size_t end,
  size_t min_width,
  const uint64_t *chunks,
  size_t chunk_count,
  size_t *start_out) {
  if (!buffer || !chunks || !start_out) return 0;
  if (chunk_count == 0) {
    if (min_width > end) return 0;
    if (min_width > 0) {
      size_t start = end - min_width;
      memset(buffer + start, '0', min_width);
      *start_out = start;
      return 1;
    }
    if (end == 0) return 0;
    buffer[end - 1U] = '0';
    *start_out = end - 1U;
    return 1;
  }

  char top_digits[20];
  size_t top_len = write_u64_decimal(top_digits, chunks[chunk_count - 1U]);
  if (chunk_count - 1U > (SIZE_MAX - top_len) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return 0;
  size_t actual_width = top_len + (chunk_count - 1U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  size_t width = min_width > actual_width ? min_width : actual_width;
  if (width > end) return 0;
  size_t start = end - width;
  size_t actual_start = end - actual_width;
  if (actual_start > start) memset(buffer + start, '0', actual_start - start);
  size_t used = actual_start;
  memcpy(buffer + used, top_digits, top_len);
  used += top_len;
  for (size_t index = chunk_count - 1U; index-- > 0;) {
    write_u64_decimal_padded19(buffer + used, chunks[index]);
    used += XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  }
  *start_out = start;
  return 1;
}

static int format_decimal_dc_write_leaf(
  const XrayScratchBigInt *value,
  char *buffer,
  size_t end,
  size_t min_width,
  size_t *start_out) {
  if (!buffer || !start_out) return 0;
  if (!value || value->count == 0) {
    static const uint64_t zero_chunks[1] = {0};
    return write_decimal_wide_chunks_tail(buffer, end, min_width, zero_chunks, 1U, start_out);
  }

  XrayScratchBigInt copy;
  xray_bigint_init(&copy);
  int ok = xray_bigint_copy(&copy, value);
  size_t cursor = end;
  while (ok && copy.count > 0) {
    uint64_t chunk = divmod_decimal_wide_chunk_preinv_inplace(&copy);
    if (copy.count == 0) {
      char top_digits[20];
      size_t top_len = write_u64_decimal_pairs(top_digits, chunk);
      if (top_len > cursor) {
        ok = 0;
        break;
      }
      size_t top_start = cursor - top_len;
      size_t natural_width = end - top_start;
      size_t width = min_width > natural_width ? min_width : natural_width;
      if (width > end) {
        ok = 0;
        break;
      }
      size_t start = end - width;
      if (start < top_start) memset(buffer + start, '0', top_start - start);
      memcpy(buffer + top_start, top_digits, top_len);
      *start_out = start;
      break;
    }
    if (cursor < XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) {
      ok = 0;
      break;
    }
    cursor -= XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
    write_u64_decimal_padded19_pairs(buffer + cursor, chunk);
  }
  xray_bigint_clear(&copy);
  return ok;
}

static int format_decimal_dc_write_internal(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *cache,
  XrayDecimalDcDivmodMode divmod_mode,
  XrayBigIntDivisionWorkspace *division_workspace,
  size_t leaf_chunks,
  unsigned int depth,
  char *buffer,
  size_t end,
  size_t min_width,
  size_t *start_out) {
  if (!value || value->count == 0) {
    static const uint64_t zero_chunks[1] = {0};
    return write_decimal_wide_chunks_tail(buffer, end, min_width, zero_chunks, 1U, start_out);
  }
  if (leaf_chunks == 0) leaf_chunks = 32U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= leaf_chunks || depth >= 64U) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(cache, split_chunks);
    if (!power) return 0;
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  int ok = decimal_dc_divmod_with_mode(
    &quotient,
    &remainder,
    value,
    power,
    divmod_mode,
    division_workspace);
  if (ok && quotient.count == 0) {
    ok = format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
    xray_bigint_clear(&quotient);
    xray_bigint_clear(&remainder);
    return ok;
  }

  if (split_chunks > SIZE_MAX / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) ok = 0;
  size_t right_width = ok ? split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS : 0;
  if (ok && right_width > end) ok = 0;
  size_t right_start = 0;
  size_t left_start = 0;
  if (ok) {
    ok = format_decimal_dc_write_internal(
      &remainder,
      cache,
      divmod_mode,
      division_workspace,
      leaf_chunks,
      depth + 1U,
      buffer,
      end,
      right_width,
      &right_start);
  }
  if (ok && right_start != end - right_width) ok = 0;
  if (ok) {
    ok = format_decimal_dc_write_internal(
      &quotient,
      cache,
      divmod_mode,
      division_workspace,
      leaf_chunks,
      depth + 1U,
      buffer,
      right_start,
      0,
      &left_start);
  }
  if (ok) {
    size_t natural_width = end - left_start;
    if (min_width > natural_width) {
      if (min_width > end) {
        ok = 0;
      } else {
        size_t padded_start = end - min_width;
        memset(buffer + padded_start, '0', left_start - padded_start);
        left_start = padded_start;
      }
    }
  }
  if (ok) *start_out = left_start;
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok;
}

static int format_decimal_dc_write_context_internal(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *power_cache,
  XrayDecimalDcContextCache *context_cache,
  XrayBigIntDivisionWorkspace *division_workspace,
  int use_preinv_qhat,
  size_t leaf_chunks,
  unsigned int depth,
  char *buffer,
  size_t end,
  size_t min_width,
  size_t *start_out) {
  if (!value || value->count == 0) {
    static const uint64_t zero_chunks[1] = {0};
    return write_decimal_wide_chunks_tail(buffer, end, min_width, zero_chunks, 1U, start_out);
  }
  if (leaf_chunks == 0) leaf_chunks = 32U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= leaf_chunks || depth >= 64U) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(power_cache, split_chunks);
    if (!power) return 0;
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  const XrayDecimalDcContextEntry *context_entry =
    decimal_dc_context_cache_get(context_cache, power_cache, split_chunks);
  if (!context_entry) return 0;
  const XrayBigIntDivisorContext *context = &context_entry->context;

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  int ordering = 0;
  int ok = 1;
  if (use_preinv_qhat) {
    ordering = xray_bigint_compare(value, &context->divisor);
    if (ordering < 0) {
      quotient.count = 0;
      ok = xray_bigint_copy(&remainder, value);
    } else if (ordering == 0) {
      remainder.count = 0;
      ok = set_u32(&quotient, 1U);
    } else if (context->divisor.count == 1U) {
      ok = divmod_bigint_u64_probe(&quotient, &remainder, value, context->divisor.limbs[0]);
    } else {
      ok = division_workspace &&
        divmod_bigint_normalized_preinv_workspace_with_inverse_probe(
          &quotient,
          &remainder,
          value,
          &context->normalized_divisor,
          context->normalization_shift,
          context_entry->normalized_top_inverse,
          division_workspace);
    }
  } else {
    ok = division_workspace ?
      xray_bigint_divmod_precomputed_workspace(&quotient, &remainder, value, context, division_workspace) :
      xray_bigint_divmod_precomputed(&quotient, &remainder, value, context);
  }
  if (ok && quotient.count == 0) {
    ok = format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
    xray_bigint_clear(&quotient);
    xray_bigint_clear(&remainder);
    return ok;
  }

  if (split_chunks > SIZE_MAX / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) ok = 0;
  size_t right_width = ok ? split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS : 0;
  if (ok && right_width > end) ok = 0;
  size_t right_start = 0;
  size_t left_start = 0;
  if (ok) {
    ok = format_decimal_dc_write_context_internal(
      &remainder,
      power_cache,
      context_cache,
      division_workspace,
      use_preinv_qhat,
      leaf_chunks,
      depth + 1U,
      buffer,
      end,
      right_width,
      &right_start);
  }
  if (ok && right_start != end - right_width) ok = 0;
  if (ok) {
    ok = format_decimal_dc_write_context_internal(
      &quotient,
      power_cache,
      context_cache,
      division_workspace,
      use_preinv_qhat,
      leaf_chunks,
      depth + 1U,
      buffer,
      right_start,
      0,
      &left_start);
  }
  if (ok) {
    size_t natural_width = end - left_start;
    if (min_width > natural_width) {
      if (min_width > end) {
        ok = 0;
      } else {
        size_t padded_start = end - min_width;
        memset(buffer + padded_start, '0', left_start - padded_start);
        left_start = padded_start;
      }
    }
  }
  if (ok) *start_out = left_start;
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok;
}

static int decimal_chunks_from_limbs_horner_direct(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t remainder = 0;
      carry = divmod_word_u32_direct(chunks[index], carry, XRAY_BIGINT_DECIMAL_CHUNK_BASE, &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = divmod_word_u32_direct(0, carry, XRAY_BIGINT_DECIMAL_CHUNK_BASE, &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint32_t reduce_65537_signed(int64_t value) {
  while (value < 0) value += XRAY_BIGINT_FERMAT_65537;
  while (value >= XRAY_BIGINT_FERMAT_65537) value -= XRAY_BIGINT_FERMAT_65537;
  return (uint32_t)value;
}

static uint32_t mod_65537_folded(const XrayScratchBigInt *value) {
  uint32_t remainder = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    int64_t folded = (int64_t)(word & 0xffffU) -
      (int64_t)((word >> 16U) & 0xffffU) +
      (int64_t)((word >> 32U) & 0xffffU) -
      (int64_t)((word >> 48U) & 0xffffU);
    remainder = reduce_65537_signed((int64_t)remainder + folded);
  }
  return remainder;
}

static int mul_add_small_inplace(XrayScratchBigInt *value, uint64_t multiplier, uint64_t addend) {
  if (value->count == 0 || multiplier == 0) return set_u64(value, addend);
  if (!reserve_limbs(value, value->count + 1)) return 0;
  uint64_t carry = addend;
  for (size_t index = 0; index < value->count; ++index) {
    carry = mul_add_small_word(value->limbs[index], multiplier, carry, &value->limbs[index]);
  }
  if (carry) value->limbs[value->count++] = carry;
  return 1;
}

static size_t write_u32_decimal(char *out, uint32_t value) {
  char digits[10];
  size_t count = 0;
  do {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value);
  for (size_t index = 0; index < count; ++index) {
    out[index] = digits[count - index - 1];
  }
  return count;
}

static size_t write_u64_decimal(char *out, uint64_t value) {
  char digits[20];
  size_t count = 0;
  do {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value);
  for (size_t index = 0; index < count; ++index) {
    out[index] = digits[count - index - 1];
  }
  return count;
}

static void copy_decimal_digit_pair(char *out, uint32_t value) {
  const char *pair = decimal_digit_pairs + ((size_t)value * 2U);
  out[0] = pair[0];
  out[1] = pair[1];
}

static size_t write_u64_decimal_pairs(char *out, uint64_t value) {
  char digits[20];
  size_t offset = sizeof(digits);
  do {
    uint64_t quotient = value / 100U;
    uint32_t pair = (uint32_t)(value - quotient * 100U);
    offset -= 2U;
    copy_decimal_digit_pair(digits + offset, pair);
    value = quotient;
  } while (value);
  if (digits[offset] == '0') offset++;
  size_t count = sizeof(digits) - offset;
  memcpy(out, digits + offset, count);
  return count;
}

static size_t write_u32_decimal_pairs(char *out, uint32_t value) {
  char digits[10];
  size_t offset = sizeof(digits);
  do {
    uint32_t quotient = value / 100U;
    uint32_t pair = value - quotient * 100U;
    offset -= 2U;
    copy_decimal_digit_pair(digits + offset, pair);
    value = quotient;
  } while (value);
  if (digits[offset] == '0') offset++;
  size_t count = sizeof(digits) - offset;
  memcpy(out, digits + offset, count);
  return count;
}

static void write_u32_decimal_padded9(char *out, uint32_t value) {
  for (size_t index = XRAY_BIGINT_DECIMAL_CHUNK_DIGITS; index-- > 0;) {
    out[index] = (char)('0' + (value % 10U));
    value /= 10U;
  }
}

static void write_u32_decimal_padded9_pairs(char *out, uint32_t value) {
  uint32_t lead = value / 100000000U;
  value -= lead * 100000000U;
  out[0] = (char)('0' + lead);
  uint32_t pair = value / 1000000U;
  value -= pair * 1000000U;
  copy_decimal_digit_pair(out + 1U, pair);
  pair = value / 10000U;
  value -= pair * 10000U;
  copy_decimal_digit_pair(out + 3U, pair);
  pair = value / 100U;
  value -= pair * 100U;
  copy_decimal_digit_pair(out + 5U, pair);
  copy_decimal_digit_pair(out + 7U, value);
}

static void write_u32_decimal_padded4_pairs(char *out, uint32_t value) {
  uint32_t pair = value / 100U;
  copy_decimal_digit_pair(out, pair);
  copy_decimal_digit_pair(out + 2U, value - pair * 100U);
}

static void write_u32_decimal_padded9_mixed_pairs(char *out, uint32_t value) {
  uint32_t lead = value / 100000000U;
  uint32_t low8 = value - lead * 100000000U;
  uint32_t high4 = low8 / 10000U;
  uint32_t low4 = low8 - high4 * 10000U;
  out[0] = (char)('0' + lead);
  write_u32_decimal_padded4_pairs(out + 1U, high4);
  write_u32_decimal_padded4_pairs(out + 5U, low4);
}

static void write_u64_decimal_padded19(char *out, uint64_t value) {
  for (size_t index = XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS; index-- > 0;) {
    out[index] = (char)('0' + (value % 10U));
    value /= 10U;
  }
}

static void write_u64_decimal_padded19_pairs(char *out, uint64_t value) {
  uint64_t lead = value / UINT64_C(1000000000000000000);
  uint64_t low18 = value - lead * UINT64_C(1000000000000000000);
  uint32_t high9 = (uint32_t)(low18 / XRAY_BIGINT_DECIMAL_CHUNK_BASE);
  uint32_t low9 = (uint32_t)(low18 - (uint64_t)high9 * XRAY_BIGINT_DECIMAL_CHUNK_BASE);
  out[0] = (char)('0' + lead);
  write_u32_decimal_padded9_pairs(out + 1U, high9);
  write_u32_decimal_padded9_pairs(out + 10U, low9);
}

static char *format_decimal_chunks_u32_mode(const uint32_t *chunks, size_t chunk_count, int writer_mode) {
  if (chunk_count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (chunk_count > (SIZE_MAX - 1U) / XRAY_BIGINT_DECIMAL_CHUNK_DIGITS) return NULL;
  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  size_t used = writer_mode ?
    write_u32_decimal_pairs(text, chunks[chunk_count - 1]) :
    write_u32_decimal(text, chunks[chunk_count - 1]);
  for (size_t index = chunk_count - 1; index-- > 0;) {
    if (writer_mode == 2) {
      write_u32_decimal_padded9_mixed_pairs(text + used, chunks[index]);
    } else if (writer_mode == 1) {
      write_u32_decimal_padded9_pairs(text + used, chunks[index]);
    } else {
      write_u32_decimal_padded9(text + used, chunks[index]);
    }
    used += XRAY_BIGINT_DECIMAL_CHUNK_DIGITS;
  }
  return text;
}

static char *format_decimal_chunks_u32(const uint32_t *chunks, size_t chunk_count, int use_pair_writer) {
  return format_decimal_chunks_u32_mode(chunks, chunk_count, use_pair_writer ? 1 : 0);
}

static char *format_decimal_chunks_u64_mode(const uint64_t *chunks, size_t chunk_count, int writer_mode) {
  if (chunk_count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (chunk_count > (SIZE_MAX - 1U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  size_t used = writer_mode ?
    write_u64_decimal_pairs(text, chunks[chunk_count - 1]) :
    write_u64_decimal(text, chunks[chunk_count - 1]);
  for (size_t index = chunk_count - 1; index-- > 0;) {
    if (writer_mode) {
      write_u64_decimal_padded19_pairs(text + used, chunks[index]);
    } else {
      write_u64_decimal_padded19(text + used, chunks[index]);
    }
    used += XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  }
  return text;
}

static char *format_decimal_chunks_u64(const uint64_t *chunks, size_t chunk_count) {
  return format_decimal_chunks_u64_mode(chunks, chunk_count, 0);
}

static char *format_decimal_chunks_u64_pair_writer(const uint64_t *chunks, size_t chunk_count) {
  return format_decimal_chunks_u64_mode(chunks, chunk_count, 1);
}

static char *get_decimal_small_limbs_u64_pair_writer(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (value->count > 3U) return NULL;

  uint64_t temp[3] = {0, 0, 0};
  uint64_t chunks[4] = {0, 0, 0, 0};
  size_t temp_count = value->count;
  memcpy(temp, value->limbs, sizeof(uint64_t) * temp_count);

  size_t chunk_count = 0;
  while (temp_count > 0) {
    uint64_t remainder = 0;
    for (size_t index = temp_count; index-- > 0;) {
      temp[index] = divmod_word_u64_direct(
        remainder,
        temp[index],
        XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
        &remainder);
    }
    if (chunk_count >= sizeof(chunks) / sizeof(chunks[0])) return NULL;
    chunks[chunk_count++] = remainder;
    while (temp_count > 0 && temp[temp_count - 1U] == 0) temp_count--;
  }

  return format_decimal_chunks_u64_pair_writer(chunks, chunk_count);
}

static int set_decimal_with_chunk_digits(XrayScratchBigInt *value, const char *decimal, unsigned int chunk_size) {
  if (!value || !decimal) return 0;
  if (chunk_size == 0 || chunk_size >= sizeof(parse_decimal_powers) / sizeof(parse_decimal_powers[0])) return 0;
  value->count = 0;
  size_t digit_count = 0;
  uint64_t chunk = 0;
  unsigned int chunk_digits = 0;
  for (const unsigned char *p = (const unsigned char *)decimal; *p; ++p) {
    unsigned char ch = *p;
    if (ch < '0' || ch > '9') {
      if (ch == ',' || ch == '_' || is_ascii_space(ch)) continue;
      return 0;
    }
    digit_count++;
    chunk = chunk * 10U + (uint64_t)(ch - '0');
    chunk_digits++;
    if (chunk_digits == chunk_size) {
      if (!mul_add_small_inplace(value, parse_decimal_powers[chunk_size], chunk)) return 0;
      chunk = 0;
      chunk_digits = 0;
    }
  }
  if (!digit_count) return 0;
  if (chunk_digits) {
    if (!mul_add_small_inplace(value, parse_decimal_powers[chunk_digits], chunk)) return 0;
  }
  return 1;
}

static unsigned int parse_chunk_digits_for_decimal(const char *decimal) {
  if (!decimal) return XRAY_BIGINT_PARSE_CHUNK_DIGITS;
  if (strlen(decimal) < XRAY_BIGINT_PARSE_LARGE_MIN_DIGITS) return XRAY_BIGINT_PARSE_CHUNK_DIGITS;
  size_t digit_count = 0;
  for (const unsigned char *p = (const unsigned char *)decimal; *p; ++p) {
    unsigned char ch = *p;
    if (ch >= '0' && ch <= '9' && ++digit_count >= XRAY_BIGINT_PARSE_LARGE_MIN_DIGITS) {
      return XRAY_BIGINT_PARSE_LARGE_CHUNK_DIGITS;
    }
  }
  return XRAY_BIGINT_PARSE_CHUNK_DIGITS;
}

int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal) {
  return set_decimal_with_chunk_digits(value, decimal, parse_chunk_digits_for_decimal(decimal));
}

int xray_bigint_set_decimal_chunk_probe(XrayScratchBigInt *value, const char *decimal, unsigned int chunk_digits) {
  return set_decimal_with_chunk_digits(value, decimal, chunk_digits);
}

static char *get_decimal_with_options_writer(const XrayScratchBigInt *value, size_t horner_min_limbs, int use_direct_divider, int use_pair_writer) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  XrayScratchBigInt copy;
  xray_bigint_init(&copy);

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (value->count >= horner_min_limbs) {
    int ok = use_direct_divider ?
      decimal_chunks_from_limbs_horner_direct(&chunks, &chunk_count, value) :
      decimal_chunks_from_limbs_horner(&chunks, &chunk_count, value);
    if (!ok) {
      xray_bigint_clear(&copy);
      return NULL;
    }
  } else {
    if (!xray_bigint_copy(&copy, value)) {
      xray_bigint_clear(&copy);
      return NULL;
    }
    if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) {
      xray_bigint_clear(&copy);
      return NULL;
    }
    while (copy.count > 0) {
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, divmod_decimal_chunk_inplace(&copy))) {
        free(chunks);
        xray_bigint_clear(&copy);
        return NULL;
      }
    }
  }
  xray_bigint_clear(&copy);

  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, use_pair_writer);
  free(chunks);
  return text;
}

static char *get_decimal_with_options(const XrayScratchBigInt *value, size_t horner_min_limbs, int use_direct_divider) {
  return get_decimal_with_options_writer(value, horner_min_limbs, use_direct_divider, 0);
}

static int use_decimal_pair_writer_route(const XrayScratchBigInt *value) {
  size_t limbs = value ? value->count : 0;
  return limbs <= XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS ||
    (limbs >= XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS &&
     limbs <= XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS);
}

static int use_decimal_dc_direct_small_window_route(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 0;
  return estimate_decimal_digits_from_bits(value) == XRAY_BIGINT_DECIMAL_DC_DIRECT_SMALL_EST_DIGITS;
}

static int use_decimal_preinv_pair_window_route(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 0;
  size_t digits = estimate_decimal_digits_from_bits(value);
  return digits >= XRAY_BIGINT_DECIMAL_PREINV_PAIR_MIN_EST_DIGITS &&
    digits <= XRAY_BIGINT_DECIMAL_PREINV_PAIR_MAX_EST_DIGITS;
}

static size_t decimal_dc_cached_preinv_leaf_chunks(size_t estimated_wide_chunks) {
  return estimated_wide_chunks >= XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_LEAF32_MIN_WIDE_CHUNKS ? 32U : 16U;
}

char *xray_bigint_get_decimal_folded_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 0);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_pair_writer_probe(const XrayScratchBigInt *value) {
  return get_decimal_with_options_writer(value, XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS, 0, 1);
}

char *xray_bigint_get_decimal_folded_pair_writer_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 1);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_mixed_pair_writer_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32_mode(chunks, chunk_count, 2);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_folded_hwdiv_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded_direct(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 0);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_folded_hwdiv_mixed_pair_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded_direct(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32_mode(chunks, chunk_count, 2);
  free(chunks);
  return text;
}

static char *get_decimal_divide_1e19_mode(const XrayScratchBigInt *value, int use_preinv, int use_pair_writer) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  if (!decimal_wide_chunks_from_limbs_divide_mode(&chunks, &chunk_count, value, use_preinv)) return NULL;
  if (chunk_count == 0) {
    uint64_t *zero_chunk = (uint64_t *)realloc(chunks, sizeof(uint64_t));
    if (!zero_chunk) {
      free(chunks);
      return NULL;
    }
    chunks = zero_chunk;
    chunks[0] = 0;
    chunk_count = 1;
  }

  char *text = use_pair_writer ?
    format_decimal_chunks_u64_pair_writer(chunks, chunk_count) :
    format_decimal_chunks_u64(chunks, chunk_count);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_divide_1e19_probe(const XrayScratchBigInt *value) {
  return get_decimal_divide_1e19_mode(value, 0, 0);
}

char *xray_bigint_get_decimal_divide_1e19_pair_writer_probe(const XrayScratchBigInt *value) {
  return get_decimal_divide_1e19_mode(value, 0, 1);
}

char *xray_bigint_get_decimal_divide_1e19_preinv_probe(const XrayScratchBigInt *value) {
  return get_decimal_divide_1e19_mode(value, 1, 0);
}

char *xray_bigint_get_decimal_divide_1e19_preinv_pair_writer_probe(const XrayScratchBigInt *value) {
  return get_decimal_divide_1e19_mode(value, 1, 1);
}

char *xray_bigint_get_decimal_dc_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 0, 0);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

char *xray_bigint_get_decimal_dc_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 0);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

char *xray_bigint_get_decimal_dc_static_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 1);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

static char *get_decimal_dc_direct_with_divmod_mode(
  const XrayScratchBigInt *value,
  size_t leaf_chunks,
  int use_static_ladder,
  XrayDecimalDcDivmodMode divmod_mode) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;

  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, use_static_ladder);
  XrayBigIntDivisionWorkspace division_workspace;
  XrayBigIntDivisionWorkspace *workspace = NULL;
  if (divmod_mode != XRAY_DECIMAL_DC_DIVMOD_DEFAULT) {
    xray_bigint_division_workspace_init(&division_workspace);
    workspace = &division_workspace;
  }
  size_t start = 0;
  int ok = format_decimal_dc_write_internal(
    value,
    &cache,
    divmod_mode,
    workspace,
    leaf_chunks ? leaf_chunks : 32U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  if (workspace) xray_bigint_division_workspace_clear(workspace);
  decimal_dc_power_cache_clear(&cache);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

static char *get_decimal_dc_direct_with_context_cache(
  const XrayScratchBigInt *value,
  size_t leaf_chunks,
  int use_static_ladder,
  int use_division_workspace) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;

  XrayDecimalDcPowerCache power_cache;
  XrayDecimalDcContextCache context_cache;
  XrayBigIntDivisionWorkspace division_workspace;
  XrayBigIntDivisionWorkspace *workspace = NULL;
  decimal_dc_power_cache_init(&power_cache, 1, use_static_ladder);
  decimal_dc_context_cache_init(&context_cache);
  if (use_division_workspace) {
    xray_bigint_division_workspace_init(&division_workspace);
    workspace = &division_workspace;
  }

  size_t start = 0;
  int ok = format_decimal_dc_write_context_internal(
    value,
    &power_cache,
    &context_cache,
    workspace,
    0,
    leaf_chunks ? leaf_chunks : 32U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  if (workspace) xray_bigint_division_workspace_clear(workspace);
  decimal_dc_context_cache_clear(&context_cache);
  decimal_dc_power_cache_clear(&power_cache);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

static char *get_decimal_dc_direct_with_reusable_context_cache(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *power_cache,
  XrayDecimalDcContextCache *context_cache,
  XrayBigIntDivisionWorkspace *workspace,
  size_t leaf_chunks,
  int use_preinv_qhat) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (!power_cache || !context_cache || (use_preinv_qhat && !workspace)) return NULL;
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;
  size_t start = 0;
  int ok = format_decimal_dc_write_context_internal(
    value,
    power_cache,
    context_cache,
    workspace,
    use_preinv_qhat,
    leaf_chunks ? leaf_chunks : 16U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

typedef struct XrayDecimalDcParallelFormatTask {
  const XrayScratchBigInt *value;
  size_t leaf_chunks;
  char *text;
  int ok;
} XrayDecimalDcParallelFormatTask;

static int run_decimal_dc_parallel_format_task(XrayDecimalDcParallelFormatTask *task) {
  if (!task || !task->value) return 0;
  task->text = get_decimal_dc_direct_with_divmod_mode(
    task->value,
    task->leaf_chunks,
    1,
    XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT);
  task->ok = task->text != NULL;
  return task->ok;
}

#if defined(_WIN32) && defined(_MSC_VER)
static unsigned __stdcall run_decimal_dc_parallel_format_thread(void *context) {
  XrayDecimalDcParallelFormatTask *task = (XrayDecimalDcParallelFormatTask *)context;
  run_decimal_dc_parallel_format_task(task);
  return 0U;
}
#endif

char *xray_bigint_get_decimal_dc_parallel_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t active_leaf = leaf_chunks ? leaf_chunks : 16U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= active_leaf) {
    return xray_bigint_get_decimal_divide_1e19_preinv_probe(value);
  }

  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 1);
  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(&cache, split_chunks);
    if (!power) {
      decimal_dc_power_cache_clear(&cache);
      return NULL;
    }
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) {
    decimal_dc_power_cache_clear(&cache);
    return xray_bigint_get_decimal_divide_1e19_preinv_probe(value);
  }

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  XrayBigIntDivisionWorkspace division_workspace;
  xray_bigint_division_workspace_init(&division_workspace);
  int ok = decimal_dc_divmod_with_mode(
    &quotient,
    &remainder,
    value,
    power,
    XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT,
    &division_workspace);
  xray_bigint_division_workspace_clear(&division_workspace);
  decimal_dc_power_cache_clear(&cache);
  if (ok && quotient.count == 0) {
    ok = 0;
  }

  XrayDecimalDcParallelFormatTask right_task;
  right_task.value = &remainder;
  right_task.leaf_chunks = active_leaf;
  right_task.text = NULL;
  right_task.ok = 0;
  char *left = NULL;
  char *combined = NULL;

#if defined(_WIN32) && defined(_MSC_VER)
  XrayWinHandle thread = NULL;
  if (ok) {
    uintptr_t handle = _beginthreadex(NULL, 0, run_decimal_dc_parallel_format_thread, &right_task, 0, NULL);
    if (handle) thread = (XrayWinHandle)handle;
  }
  if (ok) {
    left = get_decimal_dc_direct_with_divmod_mode(
      &quotient,
      active_leaf,
      1,
      XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT);
    ok = left != NULL;
  }
  if (thread) {
    WaitForSingleObject(thread, XRAY_WIN_WAIT_INFINITE);
    CloseHandle(thread);
  } else if (ok) {
    ok = run_decimal_dc_parallel_format_task(&right_task);
  }
#else
  if (ok) {
    right_task.ok = run_decimal_dc_parallel_format_task(&right_task);
    left = get_decimal_dc_direct_with_divmod_mode(
      &quotient,
      active_leaf,
      1,
      XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT);
    ok = left != NULL;
  }
#endif

  ok = ok && right_task.ok && right_task.text;
  if (ok) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right_task.text);
    size_t right_width = split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
    if (right_len > right_width || left_len > SIZE_MAX - right_width - 1U) {
      ok = 0;
    } else {
      combined = (char *)calloc(left_len + right_width + 1U, 1);
      ok = combined != NULL;
      if (ok) {
        memcpy(combined, left, left_len);
        memset(combined + left_len, '0', right_width - right_len);
        memcpy(combined + left_len + right_width - right_len, right_task.text, right_len);
      }
    }
  }
  free(left);
  free(right_task.text);
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok ? combined : NULL;
}

static char *get_decimal_dc_direct_with_cache(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *cache,
  size_t leaf_chunks,
  XrayDecimalDcDivmodMode divmod_mode,
  XrayBigIntDivisionWorkspace *reusable_workspace) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (!cache) return NULL;
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;

  XrayBigIntDivisionWorkspace division_workspace;
  XrayBigIntDivisionWorkspace *workspace = NULL;
  int owns_workspace = 0;
  if (divmod_mode != XRAY_DECIMAL_DC_DIVMOD_DEFAULT) {
    if (reusable_workspace) {
      workspace = reusable_workspace;
    } else {
      xray_bigint_division_workspace_init(&division_workspace);
      workspace = &division_workspace;
      owns_workspace = 1;
    }
  }
  size_t start = 0;
  int ok = format_decimal_dc_write_internal(
    value,
    cache,
    divmod_mode,
    workspace,
    leaf_chunks ? leaf_chunks : 16U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  if (owns_workspace) xray_bigint_division_workspace_clear(workspace);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

#if XRAY_BIGINT_HAS_THREAD_LOCAL
static XRAY_BIGINT_THREAD_LOCAL XrayDecimalDcPowerCache decimal_dc_tls_cache;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_cache_initialized;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_cache_in_use;
static XRAY_BIGINT_THREAD_LOCAL XrayBigIntDivisionWorkspace decimal_dc_tls_division_workspace;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_division_workspace_initialized;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_division_workspace_in_use;
static XRAY_BIGINT_THREAD_LOCAL XrayDecimalDcContextCache decimal_dc_tls_context_cache;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_context_cache_initialized;
static XRAY_BIGINT_THREAD_LOCAL int decimal_dc_tls_context_cache_in_use;

static void decimal_dc_tls_cache_clear_at_exit(void) {
  if (decimal_dc_tls_cache_initialized && !decimal_dc_tls_cache_in_use) {
    decimal_dc_power_cache_clear(&decimal_dc_tls_cache);
    decimal_dc_tls_cache_initialized = 0;
  }
  if (decimal_dc_tls_division_workspace_initialized && !decimal_dc_tls_division_workspace_in_use) {
    xray_bigint_division_workspace_clear(&decimal_dc_tls_division_workspace);
    decimal_dc_tls_division_workspace_initialized = 0;
  }
  if (decimal_dc_tls_context_cache_initialized && !decimal_dc_tls_context_cache_in_use) {
    decimal_dc_context_cache_clear(&decimal_dc_tls_context_cache);
    decimal_dc_tls_context_cache_initialized = 0;
  }
}

static XrayDecimalDcPowerCache *decimal_dc_tls_cache_acquire(int *using_tls) {
  if (using_tls) *using_tls = 0;
  if (decimal_dc_tls_cache_in_use) return NULL;
  if (!decimal_dc_tls_cache_initialized) {
    decimal_dc_power_cache_init(&decimal_dc_tls_cache, 1, 1);
    decimal_dc_tls_cache_initialized = 1;
    (void)atexit(decimal_dc_tls_cache_clear_at_exit);
  }
  decimal_dc_tls_cache_in_use = 1;
  if (using_tls) *using_tls = 1;
  return &decimal_dc_tls_cache;
}

static void decimal_dc_tls_cache_release(void) {
  decimal_dc_tls_cache_in_use = 0;
}

static XrayBigIntDivisionWorkspace *decimal_dc_tls_division_workspace_acquire(int *using_tls) {
  if (using_tls) *using_tls = 0;
  if (decimal_dc_tls_division_workspace_in_use) return NULL;
  if (!decimal_dc_tls_division_workspace_initialized) {
    xray_bigint_division_workspace_init(&decimal_dc_tls_division_workspace);
    decimal_dc_tls_division_workspace_initialized = 1;
    (void)atexit(decimal_dc_tls_cache_clear_at_exit);
  }
  decimal_dc_tls_division_workspace_in_use = 1;
  if (using_tls) *using_tls = 1;
  return &decimal_dc_tls_division_workspace;
}

static void decimal_dc_tls_division_workspace_release(void) {
  decimal_dc_tls_division_workspace_in_use = 0;
}

static XrayDecimalDcContextCache *decimal_dc_tls_context_cache_acquire(int *using_tls) {
  if (using_tls) *using_tls = 0;
  if (decimal_dc_tls_context_cache_in_use) return NULL;
  if (!decimal_dc_tls_context_cache_initialized) {
    decimal_dc_context_cache_init(&decimal_dc_tls_context_cache);
    decimal_dc_tls_context_cache_initialized = 1;
    (void)atexit(decimal_dc_tls_cache_clear_at_exit);
  }
  decimal_dc_tls_context_cache_in_use = 1;
  if (using_tls) *using_tls = 1;
  return &decimal_dc_tls_context_cache;
}

static void decimal_dc_tls_context_cache_release(void) {
  decimal_dc_tls_context_cache_in_use = 0;
}
#endif

char *xray_bigint_get_decimal_dc_cached_preinv_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  int using_tls = 0;
  int using_workspace_tls = 0;
  XrayDecimalDcPowerCache local_cache;
  XrayDecimalDcPowerCache *cache = NULL;
  XrayBigIntDivisionWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  cache = decimal_dc_tls_cache_acquire(&using_tls);
  workspace = decimal_dc_tls_division_workspace_acquire(&using_workspace_tls);
#endif
  if (!cache) {
    decimal_dc_power_cache_init(&local_cache, 1, 1);
    cache = &local_cache;
  }
  char *text = get_decimal_dc_direct_with_cache(
    value,
    cache,
    leaf_chunks ? leaf_chunks : 16U,
    XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT,
    workspace);
  if (using_workspace_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    decimal_dc_tls_division_workspace_release();
#endif
  }
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    decimal_dc_tls_cache_release();
#endif
  } else {
    decimal_dc_power_cache_clear(&local_cache);
  }
  return text;
}

char *xray_bigint_get_decimal_dc_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_divmod_mode(
    value,
    leaf_chunks,
    0,
    XRAY_DECIMAL_DC_DIVMOD_DEFAULT);
}

char *xray_bigint_get_decimal_dc_static_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_divmod_mode(
    value,
    leaf_chunks,
    1,
    XRAY_DECIMAL_DC_DIVMOD_DEFAULT);
}

char *xray_bigint_get_decimal_dc_workspace_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_divmod_mode(
    value,
    leaf_chunks,
    0,
    XRAY_DECIMAL_DC_DIVMOD_WORKSPACE);
}

char *xray_bigint_get_decimal_dc_preinv_qhat_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_divmod_mode(
    value,
    leaf_chunks,
    0,
    XRAY_DECIMAL_DC_DIVMOD_PREINV_QHAT);
}

char *xray_bigint_get_decimal_dc_cached_context_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_context_cache(value, leaf_chunks, 1, 0);
}

char *xray_bigint_get_decimal_dc_cached_context_workspace_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  return get_decimal_dc_direct_with_context_cache(value, leaf_chunks, 1, 1);
}

char *xray_bigint_get_decimal_wide_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  if (!decimal_wide_chunks_from_limbs_horner(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0) {
    uint64_t *zero_chunk = (uint64_t *)realloc(chunks, sizeof(uint64_t));
    if (!zero_chunk) {
      free(chunks);
      return NULL;
    }
    chunks = zero_chunk;
    chunks[0] = 0;
    chunk_count = 1;
  }
  char *text = format_decimal_chunks_u64(chunks, chunk_count);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal(const XrayScratchBigInt *value) {
  if (value && value->count <= 3U) {
    char *small_text = get_decimal_small_limbs_u64_pair_writer(value);
    if (small_text) return small_text;
  }
  if (use_decimal_dc_direct_small_window_route(value)) {
    char *direct_text = xray_bigint_get_decimal_dc_direct_probe(
      value,
      XRAY_BIGINT_DECIMAL_DC_DIRECT_SMALL_LEAF_CHUNKS);
    if (direct_text) return direct_text;
  }
  if (use_decimal_preinv_pair_window_route(value)) {
    char *cached_preinv_text = xray_bigint_get_decimal_dc_cached_preinv_probe(value, 32U);
    if (cached_preinv_text) return cached_preinv_text;
    char *preinv_pair_text = xray_bigint_get_decimal_divide_1e19_preinv_pair_writer_probe(value);
    if (preinv_pair_text) return preinv_pair_text;
  }
  if (value && value->count > 0) {
    size_t estimated_wide_chunks = estimate_decimal_wide_chunks_from_bits(value);
    char *dc_text = NULL;
    if (estimated_wide_chunks >= XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS &&
        estimated_wide_chunks <= XRAY_BIGINT_DECIMAL_DC_CACHED_PREINV_MAX_WIDE_CHUNKS) {
      size_t leaf_chunks = decimal_dc_cached_preinv_leaf_chunks(estimated_wide_chunks);
      dc_text = xray_bigint_get_decimal_dc_cached_preinv_probe(value, leaf_chunks);
    } else if (estimated_wide_chunks >= XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS) {
      dc_text = xray_bigint_get_decimal_dc_ladder_probe(value, XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS);
    }
    if (dc_text) return dc_text;
  }
  return get_decimal_with_options_writer(
    value,
    XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS,
    0,
    use_decimal_pair_writer_route(value));
}

char *xray_bigint_get_decimal_horner_threshold_probe(const XrayScratchBigInt *value, size_t horner_min_limbs) {
  return get_decimal_with_options(value, horner_min_limbs ? horner_min_limbs : 1U, 0);
}

char *xray_bigint_get_decimal_divider_probe(const XrayScratchBigInt *value, int use_direct_divider) {
  return get_decimal_with_options(value, XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS, use_direct_divider);
}

int xray_bigint_compare(const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  if (left_count < right_count) return -1;
  if (left_count > right_count) return 1;
  for (size_t index = left_count; index-- > 0;) {
    if (left->limbs[index] < right->limbs[index]) return -1;
    if (left->limbs[index] > right->limbs[index]) return 1;
  }
  return 0;
}

static int add_small_equal_count(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right || left->count != right->count || left->count == 0 || left->count > 4U) return 0;
  size_t count = left->count;
  if (!reserve_limbs(out, count + 1U)) return 0;
  unsigned char carry = 0;
#define XRAY_ADD_SMALL_EQUAL_STEP(offset) \
  carry = add_with_carry_u64(left->limbs[(offset)], right->limbs[(offset)], carry, &out->limbs[(offset)])
  switch (count) {
    case 4U:
      XRAY_ADD_SMALL_EQUAL_STEP(0U);
      XRAY_ADD_SMALL_EQUAL_STEP(1U);
      XRAY_ADD_SMALL_EQUAL_STEP(2U);
      XRAY_ADD_SMALL_EQUAL_STEP(3U);
      break;
    case 3U:
      XRAY_ADD_SMALL_EQUAL_STEP(0U);
      XRAY_ADD_SMALL_EQUAL_STEP(1U);
      XRAY_ADD_SMALL_EQUAL_STEP(2U);
      break;
    case 2U:
      XRAY_ADD_SMALL_EQUAL_STEP(0U);
      XRAY_ADD_SMALL_EQUAL_STEP(1U);
      break;
    case 1U:
      XRAY_ADD_SMALL_EQUAL_STEP(0U);
      break;
    default:
      return 0;
  }
#undef XRAY_ADD_SMALL_EQUAL_STEP
  out->count = count;
  if (carry) out->limbs[out->count++] = carry;
  return 1;
}

int xray_bigint_add(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0) return xray_bigint_copy(out, right);
  if (right->count == 0) return xray_bigint_copy(out, left);
  if (left->count == right->count && left->count <= 4U) return add_small_equal_count(out, left, right);
  const XrayScratchBigInt *longer = left;
  const XrayScratchBigInt *shorter = right;
  if (right->count > left->count) {
    longer = right;
    shorter = left;
  }
  if (!reserve_limbs(out, longer->count + 1)) return 0;
  unsigned char carry = 0;
  size_t index = 0;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (shorter->count >= 8U) {
    carry = add_limbs_u64_unroll4(out->limbs, left->limbs, right->limbs, shorter->count, 0);
    index = shorter->count;
  }
#endif
  for (; index < shorter->count; ++index) {
    carry = add_with_carry_u64(left->limbs[index], right->limbs[index], carry, &out->limbs[index]);
  }
  if (!carry && index < longer->count) {
    if (out->limbs != longer->limbs) {
      memcpy(out->limbs + index, longer->limbs + index, sizeof(uint64_t) * (longer->count - index));
    }
    out->count = longer->count;
    return 1;
  }
  for (; index < longer->count; ++index) {
    carry = add_with_carry_u64(longer->limbs[index], 0, carry, &out->limbs[index]);
  }
  out->count = longer->count;
  if (carry) out->limbs[out->count++] = carry;
  return 1;
}

static int sub_known_greater_or_equal(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (right->count == 0) return xray_bigint_copy(out, left);
  if (!reserve_limbs(out, left->count ? left->count : 1)) return 0;
  unsigned char borrow = 0;
  size_t index = 0;
  for (; index < right->count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], right->limbs[index], borrow, &out->limbs[index]);
  }
  if (!borrow && index < left->count) {
    if (out->limbs != left->limbs) {
      memcpy(out->limbs + index, left->limbs + index, sizeof(uint64_t) * (left->count - index));
    }
    out->count = left->count;
    normalize(out);
    return 1;
  }
  for (; index < left->count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], 0, borrow, &out->limbs[index]);
  }
  out->count = left->count;
  normalize(out);
  return borrow == 0;
}

static int sub_small_equal_count(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right || left->count != right->count || left->count > 4U) return 0;
  size_t count = left->count;
  for (size_t index = count; index-- > 0;) {
    if (left->limbs[index] < right->limbs[index]) return 0;
    if (left->limbs[index] > right->limbs[index]) break;
    if (index == 0) {
      out->count = 0;
      return 1;
    }
  }
  if (!reserve_limbs(out, count ? count : 1U)) return 0;
  unsigned char borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], right->limbs[index], borrow, &out->limbs[index]);
  }
  out->count = count;
  normalize(out);
  return borrow == 0;
}

int xray_bigint_sub(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  if (right_count == 0) return xray_bigint_copy(out, left);
  if (left_count < right_count) return 0;
  if (left_count == right_count) {
    if (left_count <= 4U) return sub_small_equal_count(out, left, right);
    uint64_t left_top = left->limbs[left_count - 1U];
    uint64_t right_top = right->limbs[right_count - 1U];
    if (left_top < right_top) return 0;
    if (left_top == right_top) {
      int ordering = xray_bigint_compare(left, right);
      if (ordering < 0) return 0;
      if (ordering == 0) return set_u32(out, 0);
    }
  }
  return sub_known_greater_or_equal(out, left, right);
}

static void add_two_limb_at(XrayScratchBigInt *out, size_t position, uint64_t low, uint64_t high) {
  unsigned char carry = add_with_carry_u64(out->limbs[position], low, 0, &out->limbs[position]);
  position++;
  uint64_t word = high + (uint64_t)carry;
  uint64_t extra = word < high ? 1U : 0U;
  while (word || extra) {
    carry = add_with_carry_u64(out->limbs[position], word, 0, &out->limbs[position]);
    word = extra + (uint64_t)carry;
    extra = 0;
    position++;
  }
}

static void square_add_diagonal_word(XrayScratchBigInt *out, size_t position, uint64_t word) {
  if (word == 0) return;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(word, word, &high);
  add_two_limb_at(out, position, (uint64_t)low, (uint64_t)high);
#else
  __uint128_t product = (__uint128_t)word * (__uint128_t)word;
  add_two_limb_at(out, position, (uint64_t)product, (uint64_t)(product >> XRAY_BIGINT_WORD_BITS));
#endif
}

static void add_product_at(XrayScratchBigInt *out, size_t position, uint64_t left, uint64_t right) {
  if (left == 0 || right == 0) return;
  uint64_t low = 0;
  uint64_t high = mul_add_small_word(left, right, 0, &low);
  add_two_limb_at(out, position, low, high);
}

static void square_add_doubled_product_at(XrayScratchBigInt *out, size_t position, uint64_t left, uint64_t right) {
  if (left == 0 || right == 0) return;
  uint64_t low = 0;
  uint64_t high = mul_add_small_word(left, right, 0, &low);
  uint64_t doubled_low = low << 1U;
  uint64_t doubled_high = (high << 1U) + (low >> 63U);
  uint64_t extra = high >> 63U;
  add_two_limb_at(out, position, doubled_low, doubled_high);
  if (extra) add_two_limb_at(out, position + 2U, extra, 0);
}

static void square_add_doubled_cross_row(XrayScratchBigInt *out, const uint64_t *limbs, size_t count, size_t row) {
  if (limbs[row] == 0) return;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  uint64_t carry_low = 0;
  uint64_t carry_high = 0;
  for (size_t column = row + 1U; column < count; ++column) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _umul128(limbs[row], limbs[column], &high);
    unsigned __int64 doubled_low = low << 1U;
    uint64_t carry_from_double = (uint64_t)(low >> 63U);
    unsigned __int64 sum = 0;
    unsigned char carry1 = _addcarry_u64(0, doubled_low, out->limbs[row + column], &sum);
    unsigned char carry2 = _addcarry_u64(0, sum, carry_low, &sum);
    out->limbs[row + column] = (uint64_t)sum;

    uint64_t small = carry_from_double + (uint64_t)carry1 + (uint64_t)carry2 + carry_high;
    uint64_t next_low = ((uint64_t)high) << 1U;
    uint64_t next_high = ((uint64_t)high) >> 63U;
    uint64_t next_sum = next_low + small;
    if (next_sum < next_low) next_high++;
    carry_low = next_sum;
    carry_high = next_high;
  }
  if (carry_low || carry_high) add_two_limb_at(out, row + count, carry_low, carry_high);
#else
  __uint128_t carry = 0;
  for (size_t column = row + 1U; column < count; ++column) {
    __uint128_t product = (__uint128_t)limbs[row] * (__uint128_t)limbs[column];
    uint64_t low = (uint64_t)product;
    uint64_t high = (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
    __uint128_t sum = ((__uint128_t)low << 1U) +
      (__uint128_t)out->limbs[row + column] +
      (uint64_t)carry;
    out->limbs[row + column] = (uint64_t)sum;
    carry = ((__uint128_t)high << 1U) + (sum >> XRAY_BIGINT_WORD_BITS) + (carry >> XRAY_BIGINT_WORD_BITS);
  }
  if (carry) add_two_limb_at(out, row + count, (uint64_t)carry, (uint64_t)(carry >> XRAY_BIGINT_WORD_BITS));
#endif
}

static void square_add_doubled_cross_row_unroll4(XrayScratchBigInt *out, const uint64_t *limbs, size_t count, size_t row) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (limbs[row] == 0) return;
  uint64_t carry_low = 0;
  uint64_t carry_high = 0;
  size_t column = row + 1U;
#define XRAY_SQUARE_CROSS_UNROLL4_STEP(offset) do { \
    unsigned __int64 high = 0; \
    unsigned __int64 low = _umul128(limbs[row], limbs[column + (offset)], &high); \
    unsigned __int64 doubled_low = low << 1U; \
    uint64_t carry_from_double = (uint64_t)(low >> 63U); \
    unsigned __int64 sum = 0; \
    unsigned char carry1 = _addcarry_u64(0, doubled_low, out->limbs[row + column + (offset)], &sum); \
    unsigned char carry2 = _addcarry_u64(0, sum, carry_low, &sum); \
    out->limbs[row + column + (offset)] = (uint64_t)sum; \
    uint64_t small = carry_from_double + (uint64_t)carry1 + (uint64_t)carry2 + carry_high; \
    uint64_t next_low = ((uint64_t)high) << 1U; \
    uint64_t next_high = ((uint64_t)high) >> 63U; \
    uint64_t next_sum = next_low + small; \
    if (next_sum < next_low) next_high++; \
    carry_low = next_sum; \
    carry_high = next_high; \
  } while (0)
  for (; column + 4U <= count; column += 4U) {
    XRAY_SQUARE_CROSS_UNROLL4_STEP(0U);
    XRAY_SQUARE_CROSS_UNROLL4_STEP(1U);
    XRAY_SQUARE_CROSS_UNROLL4_STEP(2U);
    XRAY_SQUARE_CROSS_UNROLL4_STEP(3U);
  }
  for (; column < count; ++column) {
    XRAY_SQUARE_CROSS_UNROLL4_STEP(0U);
  }
#undef XRAY_SQUARE_CROSS_UNROLL4_STEP
  if (carry_low || carry_high) add_two_limb_at(out, row + count, carry_low, carry_high);
#else
  square_add_doubled_cross_row(out, limbs, count, row);
#endif
}

static size_t count_nonzero_limbs(const XrayScratchBigInt *value) {
  size_t count = 0;
  if (!value) return 0;
  for (size_t index = 0; index < value->count; ++index) {
    if (value->limbs[index] != 0) count++;
  }
  return count;
}

static int count_nonzero_limbs_bounded(const XrayScratchBigInt *value, size_t max_nonzero, size_t *out_count);

static int should_use_sparse_square(const XrayScratchBigInt *value, size_t nonzero_count) {
  if (!value || value->count < XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) return 0;
  return nonzero_count > 0 && nonzero_count <= value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
}

static int collect_nonzero_indices(const XrayScratchBigInt *value, size_t *indices, size_t expected_count) {
  if (!value || !indices || expected_count == 0) return 0;
  size_t used = 0;
  for (size_t index = 0; index < value->count; ++index) {
    if (value->limbs[index] != 0) {
      if (used >= expected_count) return 0;
      indices[used++] = index;
    }
  }
  return used == expected_count;
}

static int square_schoolbook_sparse(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t nonzero_count) {
  if (!out || !value || nonzero_count == 0) return 0;
  size_t needed = value->count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  size_t stack_indices[XRAY_BIGINT_SPARSE_STACK_INDEX_CAP];
  size_t *indices = nonzero_count <= XRAY_BIGINT_SPARSE_STACK_INDEX_CAP ?
    stack_indices :
    (size_t *)malloc(sizeof(size_t) * nonzero_count);
  if (!indices) return 0;
  if (!collect_nonzero_indices(value, indices, nonzero_count)) {
    if (indices != stack_indices) free(indices);
    return 0;
  }

  for (size_t i = 0; i < nonzero_count; ++i) {
    size_t row = indices[i];
    uint64_t row_word = value->limbs[row];
    square_add_diagonal_word(out, row * 2U, row_word);
    for (size_t j = i + 1U; j < nonzero_count; ++j) {
      size_t column = indices[j];
      square_add_doubled_product_at(out, row + column, row_word, value->limbs[column]);
    }
  }

  if (indices != stack_indices) free(indices);
  normalize(out);
  return 1;
}

static int square_schoolbook_dense(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t needed = value->count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  for (size_t row = 0; row < value->count; ++row) {
    square_add_doubled_cross_row(out, value->limbs, value->count, row);
  }
  for (size_t index = 0; index < value->count; ++index) {
    square_add_diagonal_word(out, index * 2U, value->limbs[index]);
  }
  normalize(out);
  return 1;
}

static int square_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count >= XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) {
    size_t nonzero_count = count_nonzero_limbs(value);
    if (should_use_sparse_square(value, nonzero_count) &&
        square_schoolbook_sparse(out, value, nonzero_count)) {
      return 1;
    }
  }
  return square_schoolbook_dense(out, value);
}

static int square_schoolbook_fused_leaf_order(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count >= XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) {
    size_t nonzero_count = count_nonzero_limbs(value);
    if (should_use_sparse_square(value, nonzero_count) &&
        square_schoolbook_sparse(out, value, nonzero_count)) {
      return 1;
    }
  }
  size_t needed = value->count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  for (size_t row = 0; row < value->count; ++row) {
    square_add_diagonal_word(out, row * 2U, value->limbs[row]);
    square_add_doubled_cross_row(out, value->limbs, value->count, row);
  }
  normalize(out);
  return 1;
}

static int square_schoolbook_unroll4_leaf_order(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count >= XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) {
    size_t nonzero_count = count_nonzero_limbs(value);
    if (should_use_sparse_square(value, nonzero_count) &&
        square_schoolbook_sparse(out, value, nonzero_count)) {
      return 1;
    }
  }
  size_t needed = value->count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  for (size_t row = 0; row < value->count; ++row) {
    square_add_doubled_cross_row_unroll4(out, value->limbs, value->count, row);
  }
  for (size_t index = 0; index < value->count; ++index) {
    square_add_diagonal_word(out, index * 2U, value->limbs[index]);
  }
  normalize(out);
  return 1;
}

static int comba_acc_add_limb(uint64_t acc[3], size_t offset, uint64_t value) {
  if (value == 0) return 1;
  if (offset >= 3U) return 0;
  uint64_t before = acc[offset];
  acc[offset] += value;
  unsigned int carry = acc[offset] < before;
  while (carry) {
    offset++;
    if (offset >= 3U) return 0;
    before = acc[offset];
    acc[offset]++;
    carry = acc[offset] == 0;
  }
  return 1;
}

static int comba_acc_add_product(uint64_t acc[3], uint64_t left, uint64_t right, int doubled) {
  if (left == 0 || right == 0) return 1;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(left, right, &high);
  uint64_t product_low = (uint64_t)low;
  uint64_t product_high = (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)left * (__uint128_t)right;
  uint64_t product_low = (uint64_t)product;
  uint64_t product_high = (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
#endif
  if (!doubled) {
    return comba_acc_add_limb(acc, 0U, product_low) &&
      comba_acc_add_limb(acc, 1U, product_high);
  }
  uint64_t doubled_low = product_low << 1U;
  uint64_t doubled_high = (product_high << 1U) | (product_low >> 63U);
  uint64_t extra = product_high >> 63U;
  return comba_acc_add_limb(acc, 0U, doubled_low) &&
    comba_acc_add_limb(acc, 1U, doubled_high) &&
    comba_acc_add_limb(acc, 2U, extra);
}

static int square_schoolbook_comba_leaf_order(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count >= XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) {
    size_t nonzero_count = count_nonzero_limbs(value);
    if (should_use_sparse_square(value, nonzero_count) &&
        square_schoolbook_sparse(out, value, nonzero_count)) {
      return 1;
    }
  }
  size_t count = value->count;
  size_t needed = count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  uint64_t acc[3] = {0, 0, 0};
  for (size_t column_sum = 0; column_sum + 1U < needed; ++column_sum) {
    size_t i = column_sum < count ? 0 : column_sum + 1U - count;
    size_t j = column_sum - i;
    while (i < j) {
      if (!comba_acc_add_product(acc, value->limbs[i], value->limbs[j], 1)) return 0;
      i++;
      j--;
    }
    if (i == j) {
      if (!comba_acc_add_product(acc, value->limbs[i], value->limbs[i], 0)) return 0;
    }
    out->limbs[column_sum] = acc[0];
    acc[0] = acc[1];
    acc[1] = acc[2];
    acc[2] = 0;
  }
  out->limbs[needed - 1U] = acc[0];
  if (acc[1] || acc[2]) return 0;
  normalize(out);
  return 1;
}

static void square_tiny_emit_column(XrayScratchBigInt *out, uint64_t acc[3], size_t column) {
  out->limbs[column] = acc[0];
  acc[0] = acc[1];
  acc[1] = acc[2];
  acc[2] = 0;
}

static int square_tiny_dense(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  size_t count = value->count;
  if (count == 0) return set_u32(out, 0);
  if (count > 4U) return 0;

  size_t needed = count * 2U;
  if (!reserve_limbs(out, needed)) return 0;
  out->count = needed;

  uint64_t acc[3] = {0, 0, 0};
  switch (count) {
    case 4U:
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[0], 0)) return 0;
      square_tiny_emit_column(out, acc, 0U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[1], 1)) return 0;
      square_tiny_emit_column(out, acc, 1U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[2], 1)) return 0;
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[1], 0)) return 0;
      square_tiny_emit_column(out, acc, 2U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[3], 1)) return 0;
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[2], 1)) return 0;
      square_tiny_emit_column(out, acc, 3U);
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[3], 1)) return 0;
      if (!comba_acc_add_product(acc, value->limbs[2], value->limbs[2], 0)) return 0;
      square_tiny_emit_column(out, acc, 4U);
      if (!comba_acc_add_product(acc, value->limbs[2], value->limbs[3], 1)) return 0;
      square_tiny_emit_column(out, acc, 5U);
      if (!comba_acc_add_product(acc, value->limbs[3], value->limbs[3], 0)) return 0;
      square_tiny_emit_column(out, acc, 6U);
      break;
    case 3U:
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[0], 0)) return 0;
      square_tiny_emit_column(out, acc, 0U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[1], 1)) return 0;
      square_tiny_emit_column(out, acc, 1U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[2], 1)) return 0;
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[1], 0)) return 0;
      square_tiny_emit_column(out, acc, 2U);
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[2], 1)) return 0;
      square_tiny_emit_column(out, acc, 3U);
      if (!comba_acc_add_product(acc, value->limbs[2], value->limbs[2], 0)) return 0;
      square_tiny_emit_column(out, acc, 4U);
      break;
    case 2U:
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[0], 0)) return 0;
      square_tiny_emit_column(out, acc, 0U);
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[1], 1)) return 0;
      square_tiny_emit_column(out, acc, 1U);
      if (!comba_acc_add_product(acc, value->limbs[1], value->limbs[1], 0)) return 0;
      square_tiny_emit_column(out, acc, 2U);
      break;
    case 1U:
      if (!comba_acc_add_product(acc, value->limbs[0], value->limbs[0], 0)) return 0;
      square_tiny_emit_column(out, acc, 0U);
      break;
    default:
      return 0;
  }
  out->limbs[needed - 1U] = acc[0];
  if (acc[1] || acc[2]) return 0;

  normalize(out);
  return 1;
}

static int try_sparse_square_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value || value->count < XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) return 0;
  size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
  size_t nonzero_count = 0;
  if (sparse_cap == 0 || !count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
    return 0;
  }
  return square_schoolbook_sparse(out, value, nonzero_count);
}

static int slice_bigint(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t offset, size_t count);
static int add_shifted_inplace(XrayScratchBigInt *out, const XrayScratchBigInt *addend, size_t shift);
static int mul_schoolbook_mode(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  int use_unroll4,
  int use_sparse_scan);
static int square_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold, int use_fused_leaf_order);
static int square_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);

static int square_karatsuba_threshold_mode(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t threshold,
  int use_fused_leaf_order) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count <= XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
    int use_unroll4 = value->count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
#else
    int use_unroll4 = 0;
#endif
    return mul_schoolbook_mode(out, value, value, use_unroll4, 0);
  }
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (value->count < active_threshold) {
    if (use_fused_leaf_order == 3) return square_schoolbook_comba_leaf_order(out, value);
    if (use_fused_leaf_order == 2) return square_schoolbook_unroll4_leaf_order(out, value);
    return use_fused_leaf_order ?
      square_schoolbook_fused_leaf_order(out, value) :
      square_schoolbook(out, value);
  }

  size_t split = value->count / 2U;
  XrayScratchBigInt a0, a1, z0, z1, z2, sum;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum);

  int ok = slice_bigint(&a0, value, 0, split) &&
    slice_bigint(&a1, value, split, value->count - split) &&
    square_dispatch_threshold_mode(&z0, &a0, active_threshold, use_fused_leaf_order) &&
    square_dispatch_threshold_mode(&z2, &a1, active_threshold, use_fused_leaf_order) &&
    xray_bigint_add(&sum, &a0, &a1) &&
    square_dispatch_threshold_mode(&z1, &sum, active_threshold, use_fused_leaf_order) &&
    xray_bigint_sub(&z1, &z1, &z0) &&
    xray_bigint_sub(&z1, &z1, &z2);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 2U) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  xray_bigint_clear(&a0);
  xray_bigint_clear(&a1);
  xray_bigint_clear(&z0);
  xray_bigint_clear(&z1);
  xray_bigint_clear(&z2);
  xray_bigint_clear(&sum);
  return ok;
}

static int square_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  return square_karatsuba_threshold_mode(out, value, threshold, 0);
}

static int square_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold, int use_fused_leaf_order) {
  return square_karatsuba_threshold_mode(out, value, threshold, use_fused_leaf_order);
}

static int square_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  return square_karatsuba_threshold(out, value, threshold);
}

static int checked_sparse_cost(size_t left_nonzero, size_t right_nonzero, size_t *out_cost) {
  if (!out_cost || left_nonzero == 0 || right_nonzero == 0) return 0;
  if (left_nonzero > SIZE_MAX / right_nonzero) return 0;
  *out_cost = left_nonzero * right_nonzero;
  return 1;
}

static int should_use_sparse_mul(
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t left_nonzero,
  size_t right_nonzero,
  size_t best_row_cost) {
  if (!left || !right) return 0;
  size_t max_count = left->count > right->count ? left->count : right->count;
  if (max_count < XRAY_BIGINT_SPARSE_MUL_MIN_LIMBS) return 0;
  if (left_nonzero == 0 || right_nonzero == 0) return 0;
  if (left_nonzero > left->count / XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR) return 0;
  if (right_nonzero > right->count / XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR) return 0;

  size_t sparse_cost = 0;
  if (!checked_sparse_cost(left_nonzero, right_nonzero, &sparse_cost)) return 0;
  if (sparse_cost < XRAY_BIGINT_SPARSE_MUL_MIN_PRODUCTS) {
    if (max_count < XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MIN_LIMBS) return 0;
    if (sparse_cost > XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX) return 0;
  }

  return sparse_cost <= best_row_cost / 2U;
}

static int count_nonzero_limbs_bounded(const XrayScratchBigInt *value, size_t max_nonzero, size_t *out_count) {
  if (!value || !out_count || max_nonzero == 0) return 0;
  size_t nonzero = 0;
  for (size_t i = 0; i < value->count; ++i) {
    if (value->limbs[i] == 0) continue;
    nonzero++;
    if (nonzero > max_nonzero) return 0;
  }
  if (nonzero == 0) return 0;
  *out_count = nonzero;
  return 1;
}

static int mul_schoolbook_sparse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t left_nonzero,
  size_t right_nonzero) {
  if (!out || !left || !right || left_nonzero == 0 || right_nonzero == 0) return 0;
  size_t needed = left->count + right->count;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  size_t left_stack_indices[XRAY_BIGINT_SPARSE_STACK_INDEX_CAP];
  size_t right_stack_indices[XRAY_BIGINT_SPARSE_STACK_INDEX_CAP];
  size_t *left_indices = left_nonzero <= XRAY_BIGINT_SPARSE_STACK_INDEX_CAP ?
    left_stack_indices :
    (size_t *)malloc(sizeof(size_t) * left_nonzero);
  size_t *right_indices = right_nonzero <= XRAY_BIGINT_SPARSE_STACK_INDEX_CAP ?
    right_stack_indices :
    (size_t *)malloc(sizeof(size_t) * right_nonzero);
  if (!left_indices || !right_indices) {
    if (left_indices != left_stack_indices) free(left_indices);
    if (right_indices != right_stack_indices) free(right_indices);
    return 0;
  }
  if (!collect_nonzero_indices(left, left_indices, left_nonzero) ||
      !collect_nonzero_indices(right, right_indices, right_nonzero)) {
    if (left_indices != left_stack_indices) free(left_indices);
    if (right_indices != right_stack_indices) free(right_indices);
    return 0;
  }

  for (size_t i = 0; i < left_nonzero; ++i) {
    size_t left_index = left_indices[i];
    uint64_t left_word = left->limbs[left_index];
    for (size_t j = 0; j < right_nonzero; ++j) {
      size_t right_index = right_indices[j];
      add_product_at(out, left_index + right_index, left_word, right->limbs[right_index]);
    }
  }

  if (left_indices != left_stack_indices) free(left_indices);
  if (right_indices != right_stack_indices) free(right_indices);
  normalize(out);
  return 1;
}

static int try_sparse_mul_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return 0;
  size_t max_count = left->count > right->count ? left->count : right->count;
  if (max_count < XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MIN_LIMBS) return 0;

  size_t left_cap = left->count / XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR;
  size_t right_cap = right->count / XRAY_BIGINT_SPARSE_MUL_DENSITY_DIVISOR;
  if (left_cap > XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX) left_cap = XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX;
  if (right_cap > XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX) right_cap = XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX;
  size_t left_nonzero = 0;
  size_t right_nonzero = 0;
  if (!count_nonzero_limbs_bounded(left, left_cap, &left_nonzero) ||
      !count_nonzero_limbs_bounded(right, right_cap, &right_nonzero)) {
    return 0;
  }

  size_t sparse_cost = 0;
  if (!checked_sparse_cost(left_nonzero, right_nonzero, &sparse_cost) ||
      sparse_cost > XRAY_BIGINT_SPARSE_MUL_TINY_PRODUCTS_MAX) {
    return 0;
  }
  size_t left_outer_cost = left_nonzero > SIZE_MAX / right->count ? SIZE_MAX : left_nonzero * right->count;
  size_t right_outer_cost = right_nonzero > SIZE_MAX / left->count ? SIZE_MAX : right_nonzero * left->count;
  size_t best_scan_cost = left_outer_cost < right_outer_cost ? left_outer_cost : right_outer_cost;
  if (!should_use_sparse_mul(left, right, left_nonzero, right_nonzero, best_scan_cost)) return 0;
  return mul_schoolbook_sparse(out, left, right, left_nonzero, right_nonzero);
}

static int mul_schoolbook_mode(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  int use_unroll4,
  int use_sparse_scan) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  const XrayScratchBigInt *outer = left;
  const XrayScratchBigInt *inner = right;
  size_t max_count = left->count > right->count ? left->count : right->count;
  if (use_sparse_scan && max_count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS) {
    size_t left_nonzero = count_nonzero_limbs(left);
    size_t right_nonzero = count_nonzero_limbs(right);
    size_t left_outer_cost = left_nonzero > SIZE_MAX / right->count ? SIZE_MAX : left_nonzero * right->count;
    size_t right_outer_cost = right_nonzero > SIZE_MAX / left->count ? SIZE_MAX : right_nonzero * left->count;
    size_t best_scan_cost = left_outer_cost < right_outer_cost ? left_outer_cost : right_outer_cost;
    if (left_outer_cost <= SIZE_MAX - left->count) left_outer_cost += left->count;
    else left_outer_cost = SIZE_MAX;
    if (right_outer_cost <= SIZE_MAX - right->count) right_outer_cost += right->count;
    else right_outer_cost = SIZE_MAX;
    if (should_use_sparse_mul(left, right, left_nonzero, right_nonzero, best_scan_cost) &&
        mul_schoolbook_sparse(out, left, right, left_nonzero, right_nonzero)) {
      return 1;
    }
    if (right_outer_cost < left_outer_cost ||
        (right_outer_cost == left_outer_cost && outer->count > inner->count)) {
      outer = right;
      inner = left;
    }
  } else if (right->count < left->count) {
    outer = right;
    inner = left;
  }
  size_t needed = outer->count + inner->count;
  if (!reserve_limbs(out, needed)) return 0;
  out->count = needed;

  uint64_t carry = 0;
  if (outer->limbs[0] == 0) {
    memset(out->limbs, 0, sizeof(uint64_t) * needed);
  } else {
    for (size_t j = 0; j < inner->count; ++j) {
      carry = mul_add_small_word(inner->limbs[j], outer->limbs[0], carry, &out->limbs[j]);
    }
    out->limbs[inner->count] = carry;
    if (needed > inner->count + 1) {
      memset(out->limbs + inner->count + 1, 0, sizeof(uint64_t) * (needed - inner->count - 1));
    }
  }

  for (size_t i = 1; i < outer->count; ++i) {
    if (outer->limbs[i] == 0) continue;
    carry = 0;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
    if (use_unroll4) {
      carry = mul_add_word_unroll4_row(out->limbs + i, inner->limbs, outer->limbs[i], inner->count);
    } else
#else
    (void)use_unroll4;
#endif
    {
    for (size_t j = 0; j < inner->count; ++j) {
      carry = mul_add_word(out->limbs[i + j], outer->limbs[i], inner->limbs[j], carry, &out->limbs[i + j]);
    }
    }
    size_t pos = i + inner->count;
    while (carry) {
      uint64_t current = out->limbs[pos] + carry;
      out->limbs[pos] = current;
      carry = current < carry;
      pos++;
    }
  }
  normalize(out);
  return 1;
}

static int mul_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  return mul_schoolbook_mode(out, left, right, 0, 1);
}

static int slice_bigint(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t offset, size_t count) {
  if (!out || !value) return 0;
  out->count = 0;
  if (offset >= value->count || count == 0) return 1;
  size_t available = value->count - offset;
  size_t actual = count < available ? count : available;
  if (!reserve_limbs(out, actual)) return 0;
  memcpy(out->limbs, value->limbs + offset, sizeof(uint64_t) * actual);
  out->count = actual;
  normalize(out);
  return 1;
}

static void view_bigint_slice(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t offset, size_t count) {
  if (!out) return;
  out->limbs = NULL;
  out->count = 0;
  out->capacity = 0;
  if (!value || offset >= value->count || count == 0) return;
  size_t available = value->count - offset;
  size_t actual = count < available ? count : available;
  while (actual > 0 && value->limbs[offset + actual - 1U] == 0) actual--;
  if (actual == 0) return;
  out->limbs = value->limbs + offset;
  out->count = actual;
}

static int split_bigint_for_karatsuba(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t offset,
  size_t count,
  int use_slice_views) {
  if (use_slice_views) {
    view_bigint_slice(out, value, offset, count);
    return 1;
  }
  return slice_bigint(out, value, offset, count);
}

static int add_shifted_inplace(XrayScratchBigInt *out, const XrayScratchBigInt *addend, size_t shift) {
  if (!out || !addend) return 0;
  if (addend->count == 0) return 1;
  size_t needed = shift + addend->count + 1;
  if (!reserve_limbs(out, needed)) return 0;
  if (out->count < shift) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift - out->count));
    out->count = shift;
  }
  if (out->count < shift + addend->count) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift + addend->count - out->count));
    out->count = shift + addend->count;
  }
  unsigned char carry = 0;
  size_t index = 0;
  for (; index < addend->count; ++index) {
    carry = add_with_carry_u64(out->limbs[shift + index], addend->limbs[index], carry, &out->limbs[shift + index]);
  }
  size_t position = shift + index;
  while (carry) {
    if (position == out->count) {
      out->limbs[out->count++] = 0;
    }
    carry = add_with_carry_u64(out->limbs[position], 0, carry, &out->limbs[position]);
    position++;
  }
  normalize(out);
  return 1;
}

static int add_shifted_inplace_no_normalize(XrayScratchBigInt *out, const XrayScratchBigInt *addend, size_t shift) {
  if (!out || !addend) return 0;
  if (addend->count == 0) return 1;
  size_t needed = shift + addend->count + 1;
  if (!reserve_limbs(out, needed)) return 0;
  if (out->count < shift) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift - out->count));
    out->count = shift;
  }
  if (out->count < shift + addend->count) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift + addend->count - out->count));
    out->count = shift + addend->count;
  }
  unsigned char carry = 0;
  size_t index = 0;
  for (; index < addend->count; ++index) {
    carry = add_with_carry_u64(out->limbs[shift + index], addend->limbs[index], carry, &out->limbs[shift + index]);
  }
  size_t position = shift + index;
  while (carry) {
    if (position == out->count) {
      out->limbs[out->count++] = 0;
    }
    carry = add_with_carry_u64(out->limbs[position], 0, carry, &out->limbs[position]);
    position++;
  }
  return 1;
}

static int abs_diff_bigint(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  int *ordering) {
  int compare = xray_bigint_compare(left, right);
  if (ordering) *ordering = compare;
  return compare >= 0 ?
    sub_known_greater_or_equal(out, left, right) :
    sub_known_greater_or_equal(out, right, left);
}

static void clear_many_bigints(
  XrayScratchBigInt *a0,
  XrayScratchBigInt *a1,
  XrayScratchBigInt *b0,
  XrayScratchBigInt *b1,
  XrayScratchBigInt *z0,
  XrayScratchBigInt *z1,
  XrayScratchBigInt *z2,
  XrayScratchBigInt *sum_a,
  XrayScratchBigInt *sum_b) {
  xray_bigint_clear(a0);
  xray_bigint_clear(a1);
  xray_bigint_clear(b0);
  xray_bigint_clear(b1);
  xray_bigint_clear(z0);
  xray_bigint_clear(z1);
  xray_bigint_clear(z2);
  xray_bigint_clear(sum_a);
  xray_bigint_clear(sum_b);
}

static void clear_karatsuba_split_bigints(
  XrayScratchBigInt *a0,
  XrayScratchBigInt *a1,
  XrayScratchBigInt *b0,
  XrayScratchBigInt *b1,
  XrayScratchBigInt *z0,
  XrayScratchBigInt *z1,
  XrayScratchBigInt *z2,
  XrayScratchBigInt *sum_a,
  XrayScratchBigInt *sum_b,
  int use_slice_views) {
  if (!use_slice_views) {
    xray_bigint_clear(a0);
    xray_bigint_clear(a1);
    xray_bigint_clear(b0);
    xray_bigint_clear(b1);
  }
  xray_bigint_clear(z0);
  xray_bigint_clear(z1);
  xray_bigint_clear(z2);
  xray_bigint_clear(sum_a);
  xray_bigint_clear(sum_b);
}

static int mul_dispatch_threshold_mode_ex(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  int use_sparse_scan,
  int use_slice_views);
static int mul_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4);
static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);
static int mul_dispatch_threshold_sum_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4);
static int mul_toom3_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, int use_unroll4, size_t depth_limit, int use_slice_views);

static int mul_karatsuba_threshold_mode(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  int use_sparse_scan,
  int use_slice_views) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook_mode(out, left, right, use_unroll4, use_sparse_scan);
  }

  size_t split = max_count / 2U;
  XrayScratchBigInt a0, a1, b0, b1, z0, z1, z2, sum_a, sum_b;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum_a);
  xray_bigint_init(&sum_b);

  int a_order = 0;
  int b_order = 0;
  int ok = split_bigint_for_karatsuba(&a0, left, 0, split, use_slice_views) &&
    split_bigint_for_karatsuba(&a1, left, split, left_count > split ? left_count - split : 0, use_slice_views) &&
    split_bigint_for_karatsuba(&b0, right, 0, split, use_slice_views) &&
    split_bigint_for_karatsuba(&b1, right, split, right_count > split ? right_count - split : 0, use_slice_views) &&
    mul_dispatch_threshold_mode_ex(&z0, &a0, &b0, active_threshold, use_unroll4, use_sparse_scan, use_slice_views) &&
    mul_dispatch_threshold_mode_ex(&z2, &a1, &b1, active_threshold, use_unroll4, use_sparse_scan, use_slice_views) &&
    abs_diff_bigint(&sum_a, &a1, &a0, &a_order) &&
    abs_diff_bigint(&sum_b, &b1, &b0, &b_order) &&
    mul_dispatch_threshold_mode_ex(&z1, &sum_a, &sum_b, active_threshold, use_unroll4, use_sparse_scan, use_slice_views) &&
    xray_bigint_add(&sum_a, &z0, &z2) &&
    (((a_order >= 0) == (b_order >= 0)) ? xray_bigint_sub(&z1, &sum_a, &z1) : xray_bigint_add(&z1, &sum_a, &z1));

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  clear_karatsuba_split_bigints(&a0, &a1, &b0, &b1, &z0, &z1, &z2, &sum_a, &sum_b, use_slice_views);
  return ok;
}

static int mul_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  return mul_karatsuba_threshold_mode(out, left, right, threshold, 0, 1, 0);
}

static int mul_dispatch_threshold_mode_ex(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  int use_sparse_scan,
  int use_slice_views) {
  return mul_karatsuba_threshold_mode(out, left, right, threshold, use_unroll4, use_sparse_scan, use_slice_views);
}

static int mul_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  return mul_dispatch_threshold_mode_ex(out, left, right, threshold, use_unroll4, 1, 0);
}

static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  return mul_karatsuba_threshold(out, left, right, threshold);
}

typedef struct XrayKaratsubaWorkspaceFrame {
  XrayScratchBigInt z0;
  XrayScratchBigInt z1;
  XrayScratchBigInt z2;
  XrayScratchBigInt sum_a;
  XrayScratchBigInt sum_b;
} XrayKaratsubaWorkspaceFrame;

typedef struct XrayKaratsubaWorkspace {
  XrayKaratsubaWorkspaceFrame *frames;
  size_t frame_count;
} XrayKaratsubaWorkspace;

static void karatsuba_workspace_frame_init(XrayKaratsubaWorkspaceFrame *frame) {
  if (!frame) return;
  xray_bigint_init(&frame->z0);
  xray_bigint_init(&frame->z1);
  xray_bigint_init(&frame->z2);
  xray_bigint_init(&frame->sum_a);
  xray_bigint_init(&frame->sum_b);
}

static void karatsuba_workspace_frame_clear(XrayKaratsubaWorkspaceFrame *frame) {
  if (!frame) return;
  xray_bigint_clear(&frame->z0);
  xray_bigint_clear(&frame->z1);
  xray_bigint_clear(&frame->z2);
  xray_bigint_clear(&frame->sum_a);
  xray_bigint_clear(&frame->sum_b);
}

static void karatsuba_workspace_init(XrayKaratsubaWorkspace *workspace) {
  if (!workspace) return;
  workspace->frames = NULL;
  workspace->frame_count = 0;
}

static void karatsuba_workspace_clear(XrayKaratsubaWorkspace *workspace) {
  if (!workspace) return;
  for (size_t index = 0; index < workspace->frame_count; ++index) {
    karatsuba_workspace_frame_clear(&workspace->frames[index]);
  }
  free(workspace->frames);
  workspace->frames = NULL;
  workspace->frame_count = 0;
}

static size_t karatsuba_workspace_depth_for_count(size_t max_count, size_t threshold) {
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t depth = 0;
  while (max_count >= active_threshold && max_count > 1U) {
    depth++;
    max_count = (max_count + 1U) / 2U;
  }
  return depth;
}

static int karatsuba_workspace_ensure_frames(XrayKaratsubaWorkspace *workspace, size_t frame_count) {
  if (!workspace) return 0;
  if (workspace->frame_count >= frame_count) return 1;
  XrayKaratsubaWorkspaceFrame *frames = (XrayKaratsubaWorkspaceFrame *)realloc(
    workspace->frames,
    sizeof(XrayKaratsubaWorkspaceFrame) * frame_count);
  if (!frames) return 0;
  workspace->frames = frames;
  for (size_t index = workspace->frame_count; index < frame_count; ++index) {
    karatsuba_workspace_frame_init(&workspace->frames[index]);
  }
  workspace->frame_count = frame_count;
  return 1;
}

static int karatsuba_workspace_prepare(
  XrayKaratsubaWorkspace *workspace,
  size_t max_count,
  size_t threshold) {
  size_t frame_count = karatsuba_workspace_depth_for_count(max_count, threshold);
  if (!karatsuba_workspace_ensure_frames(workspace, frame_count)) return 0;
  size_t frame_count_estimate = max_count;
  for (size_t index = 0; index < frame_count; ++index) {
    if (frame_count_estimate > (SIZE_MAX - 4U) / 2U) return 0;
    size_t capacity = frame_count_estimate * 2U + 4U;
    if (capacity < 4U) capacity = 4U;
    XrayKaratsubaWorkspaceFrame *frame = &workspace->frames[index];
    if (!reserve_limbs(&frame->z0, capacity) ||
        !reserve_limbs(&frame->z1, capacity) ||
        !reserve_limbs(&frame->z2, capacity) ||
        !reserve_limbs(&frame->sum_a, capacity) ||
        !reserve_limbs(&frame->sum_b, capacity)) {
      return 0;
    }
    frame_count_estimate = (frame_count_estimate + 1U) / 2U;
  }
  return 1;
}

static void karatsuba_workspace_frame_reset(XrayKaratsubaWorkspaceFrame *frame) {
  if (!frame) return;
  frame->z0.count = 0;
  frame->z1.count = 0;
  frame->z2.count = 0;
  frame->sum_a.count = 0;
  frame->sum_b.count = 0;
}

static int mul_karatsuba_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  XrayKaratsubaWorkspace *workspace,
  size_t depth) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook_mode(out, left, right, use_unroll4, 0);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;

  size_t split = max_count / 2U;
  XrayScratchBigInt a0, a1, b0, b1;
  view_bigint_slice(&a0, left, 0, split);
  view_bigint_slice(&a1, left, split, left_count > split ? left_count - split : 0);
  view_bigint_slice(&b0, right, 0, split);
  view_bigint_slice(&b1, right, split, right_count > split ? right_count - split : 0);

  XrayKaratsubaWorkspaceFrame *frame = &workspace->frames[depth];
  karatsuba_workspace_frame_reset(frame);
  int a_order = 0;
  int b_order = 0;
  int ok = mul_karatsuba_workspace_recurse(&frame->z0, &a0, &b0, active_threshold, use_unroll4, workspace, depth + 1U) &&
    mul_karatsuba_workspace_recurse(&frame->z2, &a1, &b1, active_threshold, use_unroll4, workspace, depth + 1U) &&
    abs_diff_bigint(&frame->sum_a, &a1, &a0, &a_order) &&
    abs_diff_bigint(&frame->sum_b, &b1, &b0, &b_order) &&
    mul_karatsuba_workspace_recurse(&frame->z1, &frame->sum_a, &frame->sum_b, active_threshold, use_unroll4, workspace, depth + 1U) &&
    xray_bigint_add(&frame->sum_a, &frame->z0, &frame->z2) &&
    (((a_order >= 0) == (b_order >= 0)) ?
      xray_bigint_sub(&frame->z1, &frame->sum_a, &frame->z1) :
      xray_bigint_add(&frame->z1, &frame->sum_a, &frame->z1));

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count) &&
      add_shifted_inplace(out, &frame->z0, 0) &&
      add_shifted_inplace(out, &frame->z1, split) &&
      add_shifted_inplace(out, &frame->z2, split * 2U);
    if (ok) normalize(out);
  }
  return ok;
}

static int square_karatsuba_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t threshold,
  int use_unroll4,
  int use_mul_leaf,
  XrayKaratsubaWorkspace *workspace,
  size_t depth) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count <= XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS) {
    int active_unroll4 = use_unroll4 && value->count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
    return mul_schoolbook_mode(out, value, value, active_unroll4, 0);
  }
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (value->count < active_threshold) {
    return use_mul_leaf ?
      mul_schoolbook_mode(out, value, value, use_unroll4, 0) :
      square_schoolbook(out, value);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;

  size_t split = value->count / 2U;
  XrayScratchBigInt a0, a1;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, value->count - split);

  XrayKaratsubaWorkspaceFrame *frame = &workspace->frames[depth];
  karatsuba_workspace_frame_reset(frame);
  int diff_order = 0;
  int ok = square_karatsuba_workspace_recurse(&frame->z0, &a0, active_threshold, use_unroll4, use_mul_leaf, workspace, depth + 1U) &&
    square_karatsuba_workspace_recurse(&frame->z2, &a1, active_threshold, use_unroll4, use_mul_leaf, workspace, depth + 1U) &&
    abs_diff_bigint(&frame->sum_a, &a1, &a0, &diff_order) &&
    square_karatsuba_workspace_recurse(&frame->z1, &frame->sum_a, active_threshold, use_unroll4, use_mul_leaf, workspace, depth + 1U) &&
    xray_bigint_add(&frame->sum_b, &frame->z0, &frame->z2) &&
    xray_bigint_sub(&frame->z1, &frame->sum_b, &frame->z1);
  (void)diff_order;

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 2U) &&
      add_shifted_inplace(out, &frame->z0, 0) &&
      add_shifted_inplace(out, &frame->z1, split) &&
      add_shifted_inplace(out, &frame->z2, split * 2U);
    if (ok) normalize(out);
  }
  return ok;
}

static int square_karatsuba_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t threshold,
  int use_mul_leaf) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  int use_unroll4 = 1;
#else
  int use_unroll4 = 0;
#endif
  XrayKaratsubaWorkspace workspace;
  karatsuba_workspace_init(&workspace);
  int ok = karatsuba_workspace_prepare(&workspace, value->count, active_threshold) &&
    reserve_limbs(out, value->count * 2U + 2U) &&
    square_karatsuba_workspace_recurse(out, value, active_threshold, use_unroll4, use_mul_leaf, &workspace, 0);
  karatsuba_workspace_clear(&workspace);
  return ok;
}

static int mul_karatsuba_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t threshold) {
  if (!out || !left || !right) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  int use_unroll4 = 1;
#else
  int use_unroll4 = 0;
#endif
  XrayKaratsubaWorkspace workspace;
  karatsuba_workspace_init(&workspace);
  int ok = karatsuba_workspace_prepare(&workspace, max_count, active_threshold) &&
    reserve_limbs(out, left_count + right_count) &&
    mul_karatsuba_workspace_recurse(out, left, right, active_threshold, use_unroll4, &workspace, 0);
  karatsuba_workspace_clear(&workspace);
  return ok;
}

static int mul_karatsuba_sum_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook_mode(out, left, right, use_unroll4, 1);
  }

  size_t split = max_count / 2U;
  XrayScratchBigInt a0, a1, b0, b1, z0, z1, z2, sum_a, sum_b;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum_a);
  xray_bigint_init(&sum_b);

  int ok = slice_bigint(&a0, left, 0, split) &&
    slice_bigint(&a1, left, split, left_count > split ? left_count - split : 0) &&
    slice_bigint(&b0, right, 0, split) &&
    slice_bigint(&b1, right, split, right_count > split ? right_count - split : 0) &&
    mul_dispatch_threshold_sum_mode(&z0, &a0, &b0, active_threshold, use_unroll4) &&
    mul_dispatch_threshold_sum_mode(&z2, &a1, &b1, active_threshold, use_unroll4) &&
    xray_bigint_add(&sum_a, &a0, &a1) &&
    xray_bigint_add(&sum_b, &b0, &b1) &&
    mul_dispatch_threshold_sum_mode(&z1, &sum_a, &sum_b, active_threshold, use_unroll4) &&
    xray_bigint_sub(&z1, &z1, &z0) &&
    xray_bigint_sub(&z1, &z1, &z2);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count + 2U) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  clear_many_bigints(&a0, &a1, &b0, &b1, &z0, &z1, &z2, &sum_a, &sum_b);
  return ok;
}

static int mul_dispatch_threshold_sum_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  return mul_karatsuba_sum_threshold_mode(out, left, right, threshold, use_unroll4);
}

typedef struct XraySignedScratchBigInt {
  int sign;
  XrayScratchBigInt mag;
} XraySignedScratchBigInt;

static void signed_init(XraySignedScratchBigInt *value) {
  if (!value) return;
  value->sign = 0;
  xray_bigint_init(&value->mag);
}

static void signed_clear(XraySignedScratchBigInt *value) {
  if (!value) return;
  xray_bigint_clear(&value->mag);
  value->sign = 0;
}

static void signed_normalize(XraySignedScratchBigInt *value) {
  if (!value) return;
  normalize(&value->mag);
  if (value->mag.count == 0) value->sign = 0;
}

static int signed_set_unsigned(XraySignedScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (!xray_bigint_copy(&out->mag, value)) return 0;
  out->sign = out->mag.count ? 1 : 0;
  return 1;
}

static int signed_set_diff_unsigned(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  int compare = xray_bigint_compare(left, right);
  if (compare == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = compare > 0 ?
    sub_known_greater_or_equal(&out->mag, left, right) :
    sub_known_greater_or_equal(&out->mag, right, left);
  out->sign = ok && out->mag.count ? (compare > 0 ? 1 : -1) : 0;
  return ok;
}

static int signed_copy(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *value) {
  if (!out || !value) return 0;
  if (!xray_bigint_copy(&out->mag, &value->mag)) return 0;
  out->sign = value->sign;
  signed_normalize(out);
  return 1;
}

static int signed_add(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *left, const XraySignedScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  XraySignedScratchBigInt temp;
  signed_init(&temp);
  int ok = 1;
  if (left->sign == 0) ok = signed_copy(&temp, right);
  else if (right->sign == 0) ok = signed_copy(&temp, left);
  else if (left->sign == right->sign) {
    ok = xray_bigint_add(&temp.mag, &left->mag, &right->mag);
    temp.sign = ok && temp.mag.count ? left->sign : 0;
  } else {
    int compare = xray_bigint_compare(&left->mag, &right->mag);
    if (compare == 0) ok = set_u32(&temp.mag, 0);
    else if (compare > 0) {
      ok = xray_bigint_sub(&temp.mag, &left->mag, &right->mag);
      temp.sign = ok && temp.mag.count ? left->sign : 0;
    } else {
      ok = xray_bigint_sub(&temp.mag, &right->mag, &left->mag);
      temp.sign = ok && temp.mag.count ? right->sign : 0;
    }
  }
  if (ok) ok = signed_copy(out, &temp);
  signed_clear(&temp);
  return ok;
}

static int signed_sub(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *left, const XraySignedScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  XraySignedScratchBigInt neg_right;
  signed_init(&neg_right);
  int ok = signed_copy(&neg_right, right);
  if (ok) {
    neg_right.sign = -neg_right.sign;
    ok = signed_add(out, left, &neg_right);
  }
  signed_clear(&neg_right);
  return ok;
}

static int signed_sub_inplace(XraySignedScratchBigInt *value, const XraySignedScratchBigInt *subtrahend) {
  return signed_sub(value, value, subtrahend);
}

static int signed_divexact_u32(XraySignedScratchBigInt *value, uint32_t divisor) {
  if (!value || divisor == 0) return 0;
  if (value->sign == 0) return 1;
  XrayScratchBigInt quotient;
  xray_bigint_init(&quotient);
  uint32_t remainder = 0;
  int ok = xray_bigint_divmod_u32(&quotient, &remainder, &value->mag, divisor) && remainder == 0;
  if (ok) {
    ok = xray_bigint_copy(&value->mag, &quotient);
    signed_normalize(value);
  }
  xray_bigint_clear(&quotient);
  return ok;
}

static int signed_mul_u32_inplace(XraySignedScratchBigInt *value, uint64_t multiplier) {
  if (!value) return 0;
  if (value->sign == 0 || multiplier == 1) return 1;
  if (multiplier == 0) {
    value->sign = 0;
    value->mag.count = 0;
    return 1;
  }
  int ok = mul_add_small_inplace(&value->mag, multiplier, 0);
  signed_normalize(value);
  return ok;
}

static int signed_mul_unsigned_threshold_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  size_t threshold,
  int use_unroll4) {
  if (!out || !left || !right) return 0;
  if (left->sign == 0 || right->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = mul_dispatch_threshold_mode(&out->mag, &left->mag, &right->mag, threshold, use_unroll4);
  out->sign = ok && out->mag.count ? left->sign * right->sign : 0;
  return ok;
}

static int signed_mul_toom3_recursive_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  size_t depth_limit,
  int use_slice_views) {
  if (!out || !left || !right) return 0;
  if (left->sign == 0 || right->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = mul_toom3_probe_internal(&out->mag, &left->mag, &right->mag, threshold, use_unroll4, depth_limit, use_slice_views);
  out->sign = ok && out->mag.count ? left->sign * right->sign : 0;
  return ok;
}

static int add_scaled_unsigned(XrayScratchBigInt *out, const XrayScratchBigInt *value, uint64_t scale) {
  if (!out || !value) return 0;
  if (value->count == 0 || scale == 0) return 1;
  size_t needed = out->count > value->count ? out->count : value->count;
  if (!reserve_limbs(out, needed + 1U)) return 0;
  if (out->count < value->count) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (value->count - out->count));
    out->count = value->count;
  }

  if (scale == 1) {
    unsigned char carry = 0;
    for (size_t index = 0; index < value->count; ++index) {
      carry = add_with_carry_u64(out->limbs[index], value->limbs[index], carry, &out->limbs[index]);
    }
    size_t position = value->count;
    while (carry) {
      if (position == out->count) {
        out->limbs[out->count++] = 1;
        carry = 0;
      } else {
        carry = add_with_carry_u64(out->limbs[position], 0, carry, &out->limbs[position]);
        position++;
      }
    }
  } else {
    uint64_t carry = 0;
    for (size_t index = 0; index < value->count; ++index) {
      carry = mul_add_word(out->limbs[index], value->limbs[index], scale, carry, &out->limbs[index]);
    }
    size_t position = value->count;
    while (carry) {
      if (position == out->count) {
        out->limbs[out->count++] = carry;
        carry = 0;
      } else {
        uint64_t current = out->limbs[position] + carry;
        out->limbs[position] = current;
        carry = current < carry;
        position++;
      }
    }
  }
  normalize(out);
  return 1;
}

static int eval_toom3_positive(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2) {
  return set_u32(out, 0) &&
    add_scaled_unsigned(out, part0, 1) &&
    add_scaled_unsigned(out, part1, weight1) &&
    add_scaled_unsigned(out, part2, weight2);
}

static int add3_unsigned(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2) {
  if (!out || !part0 || !part1 || !part2) return 0;
  size_t count = part0->count;
  if (part1->count > count) count = part1->count;
  if (part2->count > count) count = part2->count;
  if (count == 0) return set_u32(out, 0);
  if (!reserve_limbs(out, count + 1U)) return 0;

  uint64_t carry = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t word01 = 0;
    uint64_t word012 = 0;
    uint64_t out_word = 0;
    uint64_t value0 = index < part0->count ? part0->limbs[index] : 0;
    uint64_t value1 = index < part1->count ? part1->limbs[index] : 0;
    uint64_t value2 = index < part2->count ? part2->limbs[index] : 0;
    unsigned char carry01 = add_with_carry_u64(value0, value1, 0, &word01);
    unsigned char carry012 = add_with_carry_u64(word01, value2, 0, &word012);
    unsigned char carry_out = carry ? add_with_carry_u64(word012, carry, 0, &out_word) : 0;
    if (!carry) out_word = word012;
    out->limbs[index] = out_word;
    carry = (uint64_t)carry01 + (uint64_t)carry012 + (uint64_t)carry_out;
  }

  out->count = count;
  if (carry) out->limbs[out->count++] = carry;
  normalize(out);
  return 1;
}

static int eval_toom3_minus_one(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2) {
  XrayScratchBigInt positive;
  xray_bigint_init(&positive);
  int ok = eval_toom3_positive(&positive, part0, part1, part2, 0, 1);
  if (ok) {
    int compare = xray_bigint_compare(&positive, part1);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, &positive, part1);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, part1, &positive);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  xray_bigint_clear(&positive);
  return ok;
}

static int signed_from_positive_eval(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2) {
  XrayScratchBigInt eval;
  xray_bigint_init(&eval);
  int ok = eval_toom3_positive(&eval, part0, part1, part2, weight1, weight2) && signed_set_unsigned(out, &eval);
  xray_bigint_clear(&eval);
  return ok;
}

static int mul_toom3_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  int use_slice_views) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t max_count = left->count > right->count ? left->count : right->count;
  size_t min_count = left->count < right->count ? left->count : right->count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || max_count < active_threshold * 3U || min_count * 3U < max_count * 2U) {
    return mul_dispatch_threshold_mode(out, left, right, active_threshold, use_unroll4);
  }

  size_t split = (max_count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2, b0, b1, b2;
  XraySignedScratchBigInt x0, x1, xm1, x2, xinf, y0, y1, ym1, y2, yinf;
  XraySignedScratchBigInt v0, v1, vm1, v2, vinf, twice_vinf;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&a2);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&b2);
  signed_init(&x0);
  signed_init(&x1);
  signed_init(&xm1);
  signed_init(&x2);
  signed_init(&xinf);
  signed_init(&y0);
  signed_init(&y1);
  signed_init(&ym1);
  signed_init(&y2);
  signed_init(&yinf);
  signed_init(&v0);
  signed_init(&v1);
  signed_init(&vm1);
  signed_init(&v2);
  signed_init(&vinf);
  signed_init(&twice_vinf);

  int ok = split_bigint_for_karatsuba(&a0, left, 0, split, use_slice_views) &&
    split_bigint_for_karatsuba(&a1, left, split, split, use_slice_views) &&
    split_bigint_for_karatsuba(&a2, left, split * 2U, left->count > split * 2U ? left->count - split * 2U : 0, use_slice_views) &&
    split_bigint_for_karatsuba(&b0, right, 0, split, use_slice_views) &&
    split_bigint_for_karatsuba(&b1, right, split, split, use_slice_views) &&
    split_bigint_for_karatsuba(&b2, right, split * 2U, right->count > split * 2U ? right->count - split * 2U : 0, use_slice_views) &&
    signed_set_unsigned(&x0, &a0) &&
    signed_from_positive_eval(&x1, &a0, &a1, &a2, 1, 1) &&
    eval_toom3_minus_one(&xm1, &a0, &a1, &a2) &&
    signed_from_positive_eval(&x2, &a0, &a1, &a2, 2, 4) &&
    signed_set_unsigned(&xinf, &a2) &&
    signed_set_unsigned(&y0, &b0) &&
    signed_from_positive_eval(&y1, &b0, &b1, &b2, 1, 1) &&
    eval_toom3_minus_one(&ym1, &b0, &b1, &b2) &&
    signed_from_positive_eval(&y2, &b0, &b1, &b2, 2, 4) &&
    signed_set_unsigned(&yinf, &b2) &&
    signed_mul_toom3_recursive_mode(&v0, &x0, &y0, active_threshold, use_unroll4, depth_limit - 1U, use_slice_views) &&
    signed_mul_toom3_recursive_mode(&v1, &x1, &y1, active_threshold, use_unroll4, depth_limit - 1U, use_slice_views) &&
    signed_mul_toom3_recursive_mode(&vm1, &xm1, &ym1, active_threshold, use_unroll4, depth_limit - 1U, use_slice_views) &&
    signed_mul_toom3_recursive_mode(&v2, &x2, &y2, active_threshold, use_unroll4, depth_limit - 1U, use_slice_views) &&
    signed_mul_toom3_recursive_mode(&vinf, &xinf, &yinf, active_threshold, use_unroll4, depth_limit - 1U, use_slice_views);

  if (ok) {
    ok = signed_sub_inplace(&v2, &vm1) &&
      signed_divexact_u32(&v2, 3) &&
      signed_sub(&vm1, &v1, &vm1) &&
      signed_divexact_u32(&vm1, 2) &&
      signed_sub_inplace(&v1, &v0) &&
      signed_sub_inplace(&v2, &v1) &&
      signed_divexact_u32(&v2, 2) &&
      signed_sub_inplace(&v1, &vm1) &&
      signed_sub_inplace(&v1, &vinf) &&
      signed_copy(&twice_vinf, &vinf) &&
      signed_mul_u32_inplace(&twice_vinf, 2) &&
      signed_sub_inplace(&v2, &twice_vinf) &&
      signed_sub_inplace(&vm1, &v2);
  }

  if (ok) {
    ok = v0.sign >= 0 && vm1.sign >= 0 && v1.sign >= 0 && v2.sign >= 0 && vinf.sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left->count + right->count + 4U) &&
      add_shifted_inplace(out, &v0.mag, 0) &&
      add_shifted_inplace(out, &vm1.mag, split) &&
      add_shifted_inplace(out, &v1.mag, split * 2U) &&
      add_shifted_inplace(out, &v2.mag, split * 3U) &&
      add_shifted_inplace(out, &vinf.mag, split * 4U);
    if (ok) normalize(out);
  }

  if (!use_slice_views) {
    xray_bigint_clear(&a0);
    xray_bigint_clear(&a1);
    xray_bigint_clear(&a2);
    xray_bigint_clear(&b0);
    xray_bigint_clear(&b1);
    xray_bigint_clear(&b2);
  }
  signed_clear(&x0);
  signed_clear(&x1);
  signed_clear(&xm1);
  signed_clear(&x2);
  signed_clear(&xinf);
  signed_clear(&y0);
  signed_clear(&y1);
  signed_clear(&ym1);
  signed_clear(&y2);
  signed_clear(&yinf);
  signed_clear(&v0);
  signed_clear(&v1);
  signed_clear(&vm1);
  signed_clear(&v2);
  signed_clear(&vinf);
  signed_clear(&twice_vinf);
  return ok;
}

#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
enum {
  XRAY_TOOM3_POINT_0 = 0,
  XRAY_TOOM3_POINT_1 = 1,
  XRAY_TOOM3_POINT_MINUS_1 = 2,
  XRAY_TOOM3_POINT_2 = 3,
  XRAY_TOOM3_POINT_INF = 4,
  XRAY_TOOM3_POINT_COUNT = 5
};

typedef struct XrayToom3WorkspaceFrame {
  XraySignedScratchBigInt x[XRAY_TOOM3_POINT_COUNT];
  XraySignedScratchBigInt y[XRAY_TOOM3_POINT_COUNT];
  XraySignedScratchBigInt v[XRAY_TOOM3_POINT_COUNT];
  XraySignedScratchBigInt twice_vinf;
  XraySignedScratchBigInt signed_temp;
  XrayScratchBigInt eval_temp;
  XrayScratchBigInt div_temp;
} XrayToom3WorkspaceFrame;

typedef struct XrayToom3Workspace {
  XrayToom3WorkspaceFrame *frames;
  size_t frame_count;
} XrayToom3Workspace;

typedef struct XrayToomPointProductTask {
  XraySignedScratchBigInt *out;
  const XraySignedScratchBigInt *left;
  const XraySignedScratchBigInt *right;
  size_t workspace_count;
  size_t threshold;
  size_t depth_limit;
  unsigned int interp_flags;
  int square_point;
  int ok;
} XrayToomPointProductTask;

#if defined(_WIN32) && defined(_MSC_VER)
static int run_toom_point_products_parallel_grouped_pool(
  XrayToomPointProductTask *tasks,
  size_t task_count,
  size_t group_count);
static int run_toom_point_products_parallel_grouped_persistent(
  XrayToomPointProductTask *tasks,
  size_t task_count,
  size_t group_count);
#endif

static int mul_toom3_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags);
static int square_toom3_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags,
  int use_mul_points,
  size_t parallel_min_count,
  size_t parallel_group_count);

static void signed_reset(XraySignedScratchBigInt *value) {
  if (!value) return;
  value->sign = 0;
  value->mag.count = 0;
}

static int signed_set_positive_eval(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2) {
  if (!out) return 0;
  int ok = eval_toom3_positive(&out->mag, part0, part1, part2, weight1, weight2);
  out->sign = ok && out->mag.count ? 1 : 0;
  return ok;
}

static int eval_toom3_minus_one_workspace(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  XrayScratchBigInt *positive) {
  if (!out || !positive) return 0;
  int ok = eval_toom3_positive(positive, part0, part1, part2, 0, 1);
  if (ok) {
    int compare = xray_bigint_compare(positive, part1);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, positive, part1);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, part1, positive);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  return ok;
}

static int eval_toom3_minus_one_abs_sum02_workspace(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  XrayScratchBigInt *sum02) {
  if (!out || !sum02) return 0;
  int ok = xray_bigint_add(sum02, part0, part2);
  if (ok) {
    int compare = xray_bigint_compare(sum02, part1);
    if (compare == 0) {
      ok = set_u32(out, 0);
    } else if (compare > 0) {
      ok = sub_known_greater_or_equal(out, sum02, part1);
    } else {
      ok = sub_known_greater_or_equal(out, part1, sum02);
    }
  }
  return ok;
}

static int eval_toom3_negative_workspace(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2,
  XrayScratchBigInt *even,
  XrayScratchBigInt *odd) {
  if (!out || !even || !odd) return 0;
  int ok = set_u32(even, 0) &&
    add_scaled_unsigned(even, part0, 1) &&
    add_scaled_unsigned(even, part2, weight2) &&
    set_u32(odd, 0) &&
    add_scaled_unsigned(odd, part1, weight1);
  if (ok) {
    int compare = xray_bigint_compare(even, odd);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, even, odd);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, odd, even);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  return ok;
}

static int signed_add_workspace(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  XraySignedScratchBigInt *temp) {
  if (!out || !left || !right || !temp) return 0;
  signed_reset(temp);
  int ok = 1;
  if (left->sign == 0) ok = signed_copy(temp, right);
  else if (right->sign == 0) ok = signed_copy(temp, left);
  else if (left->sign == right->sign) {
    ok = xray_bigint_add(&temp->mag, &left->mag, &right->mag);
    temp->sign = ok && temp->mag.count ? left->sign : 0;
  } else {
    int compare = xray_bigint_compare(&left->mag, &right->mag);
    if (compare == 0) {
      ok = set_u32(&temp->mag, 0);
      temp->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&temp->mag, &left->mag, &right->mag);
      temp->sign = ok && temp->mag.count ? left->sign : 0;
    } else {
      ok = xray_bigint_sub(&temp->mag, &right->mag, &left->mag);
      temp->sign = ok && temp->mag.count ? right->sign : 0;
    }
  }
  if (ok) ok = signed_copy(out, temp);
  return ok;
}

static int signed_sub_workspace(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  XraySignedScratchBigInt *temp) {
  if (!out || !left || !right || !temp) return 0;
  XraySignedScratchBigInt neg_right = *right;
  neg_right.sign = -neg_right.sign;
  return signed_add_workspace(out, left, &neg_right, temp);
}

static int signed_sub_inplace_workspace(
  XraySignedScratchBigInt *value,
  const XraySignedScratchBigInt *subtrahend,
  XraySignedScratchBigInt *temp) {
  return signed_sub_workspace(value, value, subtrahend, temp);
}

static int signed_divexact_u32_workspace(
  XraySignedScratchBigInt *value,
  uint32_t divisor,
  XrayScratchBigInt *quotient) {
  if (!value || !quotient || divisor == 0) return 0;
  if (value->sign == 0) return 1;
  quotient->count = 0;
  uint32_t remainder = 0;
  int ok = xray_bigint_divmod_u32(quotient, &remainder, &value->mag, divisor) && remainder == 0;
  if (ok) {
    ok = xray_bigint_copy(&value->mag, quotient);
    signed_normalize(value);
  }
  return ok;
}

static int divexact_u3_copy(XrayScratchBigInt *quotient, const XrayScratchBigInt *value) {
  if (!quotient || !value) return 0;
  if (value->count == 0) {
    quotient->count = 0;
    return 1;
  }
  if (!reserve_limbs(quotient, value->count)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    uint64_t digit = (word - carry) * XRAY_BIGINT_DIVEXACT_3_INVERSE;
    uint64_t product_low = 0;
    uint64_t product_high = mul_add_small_word(digit, 3U, carry, &product_low);
    if (product_low != word || product_high > 2U) return 0;
    quotient->limbs[index] = digit;
    carry = product_high;
  }
  if (carry != 0) return 0;
  quotient->count = value->count;
  normalize(quotient);
  return 1;
}

static int signed_divexact_u3_workspace(
  XraySignedScratchBigInt *value,
  XrayScratchBigInt *quotient) {
  if (!value || !quotient) return 0;
  if (value->sign == 0) return 1;
  quotient->count = 0;
  int ok = divexact_u3_copy(quotient, &value->mag) &&
    xray_bigint_copy(&value->mag, quotient);
  if (ok) signed_normalize(value);
  return ok;
}

static int divexact_u5_copy(XrayScratchBigInt *quotient, const XrayScratchBigInt *value) {
  if (!quotient || !value) return 0;
  if (value->count == 0) {
    quotient->count = 0;
    return 1;
  }
  if (!reserve_limbs(quotient, value->count)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    uint64_t digit = (word - carry) * XRAY_BIGINT_DIVEXACT_5_INVERSE;
    uint64_t product_low = 0;
    uint64_t product_high = mul_add_small_word(digit, 5U, carry, &product_low);
    if (product_low != word || product_high > 4U) return 0;
    quotient->limbs[index] = digit;
    carry = product_high;
  }
  if (carry != 0) return 0;
  quotient->count = value->count;
  normalize(quotient);
  return 1;
}

static int signed_divexact_u5_workspace(
  XraySignedScratchBigInt *value,
  XrayScratchBigInt *quotient) {
  if (!value || !quotient) return 0;
  if (value->sign == 0) return 1;
  quotient->count = 0;
  int ok = divexact_u5_copy(quotient, &value->mag) &&
    xray_bigint_copy(&value->mag, quotient);
  if (ok) signed_normalize(value);
  return ok;
}

static int divexact_u3_inplace(XrayScratchBigInt *value) {
  if (!value) return 0;
  if (value->count == 0) return 1;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    uint64_t digit = (word - carry) * XRAY_BIGINT_DIVEXACT_3_INVERSE;
    uint64_t product_low = 0;
    uint64_t product_high = mul_add_small_word(digit, 3U, carry, &product_low);
    if (product_low != word || product_high > 2U) return 0;
    value->limbs[index] = digit;
    carry = product_high;
  }
  if (carry != 0) return 0;
  normalize(value);
  return 1;
}

static int signed_divexact_u3_inplace_workspace(XraySignedScratchBigInt *value) {
  if (!value) return 0;
  if (value->sign == 0) return 1;
  int ok = divexact_u3_inplace(&value->mag);
  if (ok) signed_normalize(value);
  return ok;
}

static int shift_right_bits_inplace_exact(XrayScratchBigInt *value, unsigned int shift) {
  if (!value || shift == 0 || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (value->count == 0) return 1;
  uint64_t low_mask = (UINT64_C(1) << shift) - 1U;
  if ((value->limbs[0] & low_mask) != 0) return 0;
  uint64_t carry = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1U;
    uint64_t word = value->limbs[index];
    value->limbs[index] = (word >> shift) | (carry << (XRAY_BIGINT_WORD_BITS - shift));
    carry = word & low_mask;
  }
  normalize(value);
  return carry == 0;
}

static int signed_divexact_pow2_inplace_workspace(
  XraySignedScratchBigInt *value,
  unsigned int shift) {
  if (!value) return 0;
  if (value->sign == 0) return 1;
  int ok = shift_right_bits_inplace_exact(&value->mag, shift);
  if (ok) signed_normalize(value);
  return ok;
}

static int signed_divexact_pow2_workspace(
  XraySignedScratchBigInt *value,
  unsigned int shift,
  XrayScratchBigInt *quotient) {
  if (!value || !quotient || shift == 0 || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (value->sign == 0) return 1;
  uint64_t low_mask = (UINT64_C(1) << shift) - 1U;
  if (value->mag.count > 0 && (value->mag.limbs[0] & low_mask) != 0) return 0;
  quotient->count = 0;
  int ok = shift_right_bits_copy(quotient, &value->mag, shift) &&
    xray_bigint_copy(&value->mag, quotient);
  if (ok) signed_normalize(value);
  return ok;
}

static void toom3_workspace_frame_init(XrayToom3WorkspaceFrame *frame) {
  if (!frame) return;
  for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) {
    signed_init(&frame->x[index]);
    signed_init(&frame->y[index]);
    signed_init(&frame->v[index]);
  }
  signed_init(&frame->twice_vinf);
  signed_init(&frame->signed_temp);
  xray_bigint_init(&frame->eval_temp);
  xray_bigint_init(&frame->div_temp);
}

static void toom3_workspace_frame_clear(XrayToom3WorkspaceFrame *frame) {
  if (!frame) return;
  for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) {
    signed_clear(&frame->x[index]);
    signed_clear(&frame->y[index]);
    signed_clear(&frame->v[index]);
  }
  signed_clear(&frame->twice_vinf);
  signed_clear(&frame->signed_temp);
  xray_bigint_clear(&frame->eval_temp);
  xray_bigint_clear(&frame->div_temp);
}

static void toom3_workspace_frame_reset(XrayToom3WorkspaceFrame *frame) {
  if (!frame) return;
  for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) {
    signed_reset(&frame->x[index]);
    signed_reset(&frame->y[index]);
    signed_reset(&frame->v[index]);
  }
  signed_reset(&frame->twice_vinf);
  signed_reset(&frame->signed_temp);
  frame->eval_temp.count = 0;
  frame->div_temp.count = 0;
}

static void toom3_workspace_init(XrayToom3Workspace *workspace) {
  if (!workspace) return;
  workspace->frames = NULL;
  workspace->frame_count = 0;
}

static void toom3_workspace_clear(XrayToom3Workspace *workspace) {
  if (!workspace) return;
  for (size_t index = 0; index < workspace->frame_count; ++index) {
    toom3_workspace_frame_clear(&workspace->frames[index]);
  }
  free(workspace->frames);
  workspace->frames = NULL;
  workspace->frame_count = 0;
}

static int toom3_workspace_ensure_frames(XrayToom3Workspace *workspace, size_t frame_count) {
  if (!workspace) return 0;
  if (workspace->frame_count >= frame_count) return 1;
  XrayToom3WorkspaceFrame *frames = (XrayToom3WorkspaceFrame *)realloc(
    workspace->frames,
    sizeof(XrayToom3WorkspaceFrame) * frame_count);
  if (!frames) return 0;
  workspace->frames = frames;
  for (size_t index = workspace->frame_count; index < frame_count; ++index) {
    toom3_workspace_frame_init(&workspace->frames[index]);
  }
  workspace->frame_count = frame_count;
  return 1;
}

static int toom3_workspace_reserve_signed(XraySignedScratchBigInt *value, size_t capacity) {
  return value && reserve_limbs(&value->mag, capacity);
}

static int toom3_workspace_frame_reserve(XrayToom3WorkspaceFrame *frame, size_t capacity) {
  if (!frame) return 0;
  for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) {
    if (!toom3_workspace_reserve_signed(&frame->x[index], capacity) ||
        !toom3_workspace_reserve_signed(&frame->y[index], capacity) ||
        !toom3_workspace_reserve_signed(&frame->v[index], capacity)) {
      return 0;
    }
  }
  return toom3_workspace_reserve_signed(&frame->twice_vinf, capacity) &&
    toom3_workspace_reserve_signed(&frame->signed_temp, capacity) &&
    reserve_limbs(&frame->eval_temp, capacity) &&
    reserve_limbs(&frame->div_temp, capacity);
}

static int toom3_workspace_prepare(
  XrayToom3Workspace *workspace,
  size_t max_count,
  size_t depth_limit) {
  size_t frame_count = depth_limit >= 1U ? depth_limit : 1U;
  if (!toom3_workspace_ensure_frames(workspace, frame_count)) return 0;
  size_t frame_count_estimate = max_count ? max_count : 1U;
  for (size_t index = 0; index < frame_count; ++index) {
    if (frame_count_estimate > (SIZE_MAX - 16U) / 2U) return 0;
    size_t capacity = frame_count_estimate * 2U + 16U;
    if (capacity < 16U) capacity = 16U;
    if (!toom3_workspace_frame_reserve(&workspace->frames[index], capacity)) return 0;
    frame_count_estimate = (frame_count_estimate + 2U) / 3U + 4U;
  }
  return 1;
}

static int signed_mul_toom3_workspace_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags) {
  if (!out || !left || !right) return 0;
  if (left->sign == 0 || right->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = mul_toom3_workspace_recurse(&out->mag, &left->mag, &right->mag, threshold, use_unroll4, depth_limit, workspace, karatsuba_workspace, depth, interp_flags);
  out->sign = ok && out->mag.count ? left->sign * right->sign : 0;
  return ok;
}

static int signed_square_toom3_workspace_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *value,
  size_t threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags,
  int use_mul_points) {
  if (!out || !value) return 0;
  if (value->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = use_mul_points ?
    mul_toom3_workspace_recurse(&out->mag, &value->mag, &value->mag, threshold, use_unroll4, depth_limit, workspace, karatsuba_workspace, depth, interp_flags) :
    square_toom3_workspace_recurse(&out->mag, &value->mag, threshold, use_unroll4, depth_limit, workspace, karatsuba_workspace, depth, interp_flags, 0, 0U, 0U);
  out->sign = ok && out->mag.count ? 1 : 0;
  return ok;
}

static int mul_toom3_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t max_count = left->count > right->count ? left->count : right->count;
  size_t min_count = left->count < right->count ? left->count : right->count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || max_count < active_threshold * 3U || min_count * 3U < max_count * 2U) {
    if (karatsuba_workspace) {
      return mul_karatsuba_workspace_recurse(out, left, right, active_threshold, use_unroll4, karatsuba_workspace, 0);
    }
    return mul_dispatch_threshold_mode_ex(out, left, right, active_threshold, use_unroll4, 1, 1);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;

  size_t split = (max_count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2, b0, b1, b2;
  view_bigint_slice(&a0, left, 0, split);
  view_bigint_slice(&a1, left, split, split);
  view_bigint_slice(&a2, left, split * 2U, left->count > split * 2U ? left->count - split * 2U : 0);
  view_bigint_slice(&b0, right, 0, split);
  view_bigint_slice(&b1, right, split, split);
  view_bigint_slice(&b2, right, split * 2U, right->count > split * 2U ? right->count - split * 2U : 0);

  XrayToom3WorkspaceFrame *frame = &workspace->frames[depth];
  toom3_workspace_frame_reset(frame);
  int ok = signed_set_unsigned(&frame->x[XRAY_TOOM3_POINT_0], &a0) &&
    signed_set_positive_eval(&frame->x[XRAY_TOOM3_POINT_1], &a0, &a1, &a2, 1, 1) &&
    eval_toom3_minus_one_workspace(&frame->x[XRAY_TOOM3_POINT_MINUS_1], &a0, &a1, &a2, &frame->eval_temp) &&
    ((interp_flags & XRAY_TOOM3_INTERP_EVAL_NEG2) ?
      eval_toom3_negative_workspace(&frame->x[XRAY_TOOM3_POINT_2], &a0, &a1, &a2, 2, 4, &frame->eval_temp, &frame->div_temp) :
      signed_set_positive_eval(&frame->x[XRAY_TOOM3_POINT_2], &a0, &a1, &a2, 2, 4)) &&
    signed_set_unsigned(&frame->x[XRAY_TOOM3_POINT_INF], &a2) &&
    signed_set_unsigned(&frame->y[XRAY_TOOM3_POINT_0], &b0) &&
    signed_set_positive_eval(&frame->y[XRAY_TOOM3_POINT_1], &b0, &b1, &b2, 1, 1) &&
    eval_toom3_minus_one_workspace(&frame->y[XRAY_TOOM3_POINT_MINUS_1], &b0, &b1, &b2, &frame->eval_temp) &&
    ((interp_flags & XRAY_TOOM3_INTERP_EVAL_NEG2) ?
      eval_toom3_negative_workspace(&frame->y[XRAY_TOOM3_POINT_2], &b0, &b1, &b2, 2, 4, &frame->eval_temp, &frame->div_temp) :
      signed_set_positive_eval(&frame->y[XRAY_TOOM3_POINT_2], &b0, &b1, &b2, 2, 4)) &&
    signed_set_unsigned(&frame->y[XRAY_TOOM3_POINT_INF], &b2) &&
    signed_mul_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_0],
      &frame->x[XRAY_TOOM3_POINT_0],
      &frame->y[XRAY_TOOM3_POINT_0],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    signed_mul_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_1],
      &frame->x[XRAY_TOOM3_POINT_1],
      &frame->y[XRAY_TOOM3_POINT_1],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    signed_mul_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_MINUS_1],
      &frame->x[XRAY_TOOM3_POINT_MINUS_1],
      &frame->y[XRAY_TOOM3_POINT_MINUS_1],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    signed_mul_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_2],
      &frame->x[XRAY_TOOM3_POINT_2],
      &frame->y[XRAY_TOOM3_POINT_2],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    signed_mul_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_INF],
      &frame->x[XRAY_TOOM3_POINT_INF],
      &frame->y[XRAY_TOOM3_POINT_INF],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags);

  XraySignedScratchBigInt *v0 = &frame->v[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *v1 = &frame->v[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *vm1 = &frame->v[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *v2 = &frame->v[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *vinf = &frame->v[XRAY_TOOM3_POINT_INF];

  if (ok && (interp_flags & XRAY_TOOM3_INTERP_EVAL_NEG2)) {
    ok = signed_sub_inplace_workspace(v2, v1, &frame->signed_temp) &&
      ((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) ?
        signed_divexact_u3_inplace_workspace(v2) :
      ((interp_flags & XRAY_TOOM3_INTERP_EXACT_DIV3) ?
        signed_divexact_u3_workspace(v2, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 3, &frame->div_temp))) &&
      signed_sub_workspace(v1, vm1, v1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(v1, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(v1, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(v1, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(vm1, v0, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v2, vm1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(v2, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(v2, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(vm1, v1, &frame->signed_temp) &&
      signed_sub_inplace_workspace(vm1, vinf, &frame->signed_temp) &&
      signed_copy(&frame->twice_vinf, vinf) &&
      signed_mul_u32_inplace(&frame->twice_vinf, 2) &&
      signed_sub_inplace_workspace(v2, &frame->twice_vinf, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v1, v2, &frame->signed_temp);
    if (ok) {
      if (v1->sign) v1->sign = -v1->sign;
      if (v2->sign) v2->sign = -v2->sign;
      XraySignedScratchBigInt *c1 = v1;
      XraySignedScratchBigInt *c2 = vm1;
      vm1 = c1;
      v1 = c2;
    }
  } else if (ok) {
    ok = signed_sub_inplace_workspace(v2, vm1, &frame->signed_temp) &&
      ((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) ?
        signed_divexact_u3_inplace_workspace(v2) :
      ((interp_flags & XRAY_TOOM3_INTERP_EXACT_DIV3) ?
        signed_divexact_u3_workspace(v2, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 3, &frame->div_temp))) &&
      signed_sub_workspace(vm1, v1, vm1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(vm1, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(vm1, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(vm1, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(v1, v0, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v2, v1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(v2, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(v2, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(v1, vm1, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v1, vinf, &frame->signed_temp) &&
      signed_copy(&frame->twice_vinf, vinf) &&
      signed_mul_u32_inplace(&frame->twice_vinf, 2) &&
      signed_sub_inplace_workspace(v2, &frame->twice_vinf, &frame->signed_temp) &&
      signed_sub_inplace_workspace(vm1, v2, &frame->signed_temp);
  }

  if (ok) {
    ok = v0->sign >= 0 && vm1->sign >= 0 && v1->sign >= 0 && v2->sign >= 0 && vinf->sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left->count + right->count + 4U) &&
      add_shifted_inplace(out, &v0->mag, 0) &&
      add_shifted_inplace(out, &vm1->mag, split) &&
      add_shifted_inplace(out, &v1->mag, split * 2U) &&
      add_shifted_inplace(out, &v2->mag, split * 3U) &&
      add_shifted_inplace(out, &vinf->mag, split * 4U);
    if (ok) normalize(out);
  }
  return ok;
}

static int square_toom3_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags,
  int use_mul_points,
  size_t parallel_min_count,
  size_t parallel_group_count) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || value->count < active_threshold * 3U) {
    if (karatsuba_workspace) {
      return square_karatsuba_workspace_recurse(out, value, active_threshold, use_unroll4, 0, karatsuba_workspace, 0);
    }
    return mul_dispatch_threshold_mode_ex(out, value, value, active_threshold, use_unroll4, 1, 1);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;

  size_t split = (value->count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, value->count > split * 2U ? value->count - split * 2U : 0);

  XrayToom3WorkspaceFrame *frame = &workspace->frames[depth];
  toom3_workspace_frame_reset(frame);
  int ok = signed_set_unsigned(&frame->x[XRAY_TOOM3_POINT_0], &a0) &&
    signed_set_positive_eval(&frame->x[XRAY_TOOM3_POINT_1], &a0, &a1, &a2, 1, 1) &&
    eval_toom3_minus_one_workspace(&frame->x[XRAY_TOOM3_POINT_MINUS_1], &a0, &a1, &a2, &frame->eval_temp) &&
    ((interp_flags & XRAY_TOOM3_INTERP_EVAL_NEG2) ?
      eval_toom3_negative_workspace(&frame->x[XRAY_TOOM3_POINT_2], &a0, &a1, &a2, 2, 4, &frame->eval_temp, &frame->div_temp) :
      signed_set_positive_eval(&frame->x[XRAY_TOOM3_POINT_2], &a0, &a1, &a2, 2, 4)) &&
    signed_set_unsigned(&frame->x[XRAY_TOOM3_POINT_INF], &a2);

  int point_products_ready = 0;
  if (ok && !use_mul_points && depth == 0 && parallel_min_count > 0U && value->count >= parallel_min_count) {
#if defined(_WIN32) && defined(_MSC_VER)
    XrayToomPointProductTask tasks[XRAY_TOOM3_POINT_COUNT];
    for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) {
      tasks[index].out = &frame->v[index];
      tasks[index].left = &frame->x[index];
      tasks[index].right = &frame->x[index];
      tasks[index].workspace_count = value->count;
      tasks[index].threshold = active_threshold;
      tasks[index].depth_limit = depth_limit > 0U ? depth_limit - 1U : 0U;
      tasks[index].interp_flags = interp_flags;
      tasks[index].square_point = 1;
      tasks[index].ok = 0;
    }
    size_t point_group_count = parallel_group_count > 0U ? parallel_group_count : 3U;
    point_products_ready = run_toom_point_products_parallel_grouped_persistent(
      tasks,
      XRAY_TOOM3_POINT_COUNT,
      point_group_count);
    if (!point_products_ready) {
      point_products_ready = run_toom_point_products_parallel_grouped_pool(
        tasks,
        XRAY_TOOM3_POINT_COUNT,
        point_group_count);
    }
    if (!point_products_ready) {
      for (size_t index = 0; index < XRAY_TOOM3_POINT_COUNT; ++index) signed_reset(&frame->v[index]);
    }
#else
    point_products_ready = 0;
#endif
  }

  if (ok && !point_products_ready) {
    ok = signed_square_toom3_workspace_mode(
      &frame->v[XRAY_TOOM3_POINT_0],
      &frame->x[XRAY_TOOM3_POINT_0],
      active_threshold,
      use_unroll4,
      depth_limit - 1U,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags,
      use_mul_points) &&
      signed_square_toom3_workspace_mode(
        &frame->v[XRAY_TOOM3_POINT_1],
        &frame->x[XRAY_TOOM3_POINT_1],
        active_threshold,
        use_unroll4,
        depth_limit - 1U,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        use_mul_points) &&
      signed_square_toom3_workspace_mode(
        &frame->v[XRAY_TOOM3_POINT_MINUS_1],
        &frame->x[XRAY_TOOM3_POINT_MINUS_1],
        active_threshold,
        use_unroll4,
        depth_limit - 1U,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        use_mul_points) &&
      signed_square_toom3_workspace_mode(
        &frame->v[XRAY_TOOM3_POINT_2],
        &frame->x[XRAY_TOOM3_POINT_2],
        active_threshold,
        use_unroll4,
        depth_limit - 1U,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        use_mul_points) &&
      signed_square_toom3_workspace_mode(
        &frame->v[XRAY_TOOM3_POINT_INF],
        &frame->x[XRAY_TOOM3_POINT_INF],
        active_threshold,
        use_unroll4,
        depth_limit - 1U,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        use_mul_points);
  }

  XraySignedScratchBigInt *v0 = &frame->v[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *v1 = &frame->v[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *vm1 = &frame->v[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *v2 = &frame->v[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *vinf = &frame->v[XRAY_TOOM3_POINT_INF];

  if (ok && (interp_flags & XRAY_TOOM3_INTERP_EVAL_NEG2)) {
    ok = signed_sub_inplace_workspace(v2, v1, &frame->signed_temp) &&
      ((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) ?
        signed_divexact_u3_inplace_workspace(v2) :
      ((interp_flags & XRAY_TOOM3_INTERP_EXACT_DIV3) ?
        signed_divexact_u3_workspace(v2, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 3, &frame->div_temp))) &&
      signed_sub_workspace(v1, vm1, v1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(v1, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(v1, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(v1, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(vm1, v0, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v2, vm1, &frame->signed_temp) &&
      (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
        signed_divexact_pow2_inplace_workspace(v2, 1) :
      ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
        signed_divexact_pow2_workspace(v2, 1, &frame->div_temp) :
        signed_divexact_u32_workspace(v2, 2, &frame->div_temp))) &&
      signed_sub_inplace_workspace(vm1, v1, &frame->signed_temp) &&
      signed_sub_inplace_workspace(vm1, vinf, &frame->signed_temp) &&
      signed_copy(&frame->twice_vinf, vinf) &&
      signed_mul_u32_inplace(&frame->twice_vinf, 2) &&
      signed_sub_inplace_workspace(v2, &frame->twice_vinf, &frame->signed_temp) &&
      signed_sub_inplace_workspace(v1, v2, &frame->signed_temp);
    if (ok) {
      if (v1->sign) v1->sign = -v1->sign;
      if (v2->sign) v2->sign = -v2->sign;
      XraySignedScratchBigInt *c1 = v1;
      XraySignedScratchBigInt *c2 = vm1;
      vm1 = c1;
      v1 = c2;
    }
  } else if (ok) {
    if (v0->sign >= 0 && vm1->sign >= 0 && v1->sign >= 0 && v2->sign >= 0 && vinf->sign >= 0) {
      ok = sub_known_greater_or_equal(&frame->div_temp, &v1->mag, &vm1->mag) &&
        shift_right_bits_inplace_exact(&frame->div_temp, 1) &&
        xray_bigint_add(&frame->eval_temp, &v1->mag, &vm1->mag) &&
        shift_right_bits_inplace_exact(&frame->eval_temp, 1) &&
        sub_known_greater_or_equal(&frame->eval_temp, &frame->eval_temp, &v0->mag) &&
        sub_known_greater_or_equal(&v1->mag, &frame->eval_temp, &vinf->mag) &&
        sub_known_greater_or_equal(&frame->eval_temp, &v2->mag, &v0->mag) &&
        signed_copy(&frame->twice_vinf, vinf) &&
        signed_mul_u32_inplace(&frame->twice_vinf, 16) &&
        sub_known_greater_or_equal(&frame->eval_temp, &frame->eval_temp, &frame->twice_vinf.mag) &&
        shift_right_bits_inplace_exact(&frame->eval_temp, 1) &&
        signed_set_unsigned(&frame->signed_temp, &v1->mag) &&
        signed_mul_u32_inplace(&frame->signed_temp, 2) &&
        sub_known_greater_or_equal(&frame->eval_temp, &frame->eval_temp, &frame->div_temp) &&
        sub_known_greater_or_equal(&frame->eval_temp, &frame->eval_temp, &frame->signed_temp.mag) &&
        divexact_u3_inplace(&frame->eval_temp) &&
        xray_bigint_copy(&v2->mag, &frame->eval_temp) &&
        sub_known_greater_or_equal(&vm1->mag, &frame->div_temp, &v2->mag);
      vm1->sign = ok && vm1->mag.count ? 1 : 0;
      v1->sign = ok && v1->mag.count ? 1 : 0;
      v2->sign = ok && v2->mag.count ? 1 : 0;
    } else {
      ok = signed_sub_inplace_workspace(v2, vm1, &frame->signed_temp) &&
        ((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) ?
          signed_divexact_u3_inplace_workspace(v2) :
        ((interp_flags & XRAY_TOOM3_INTERP_EXACT_DIV3) ?
          signed_divexact_u3_workspace(v2, &frame->div_temp) :
          signed_divexact_u32_workspace(v2, 3, &frame->div_temp))) &&
        signed_sub_workspace(vm1, v1, vm1, &frame->signed_temp) &&
        (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
          signed_divexact_pow2_inplace_workspace(vm1, 1) :
        ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
          signed_divexact_pow2_workspace(vm1, 1, &frame->div_temp) :
          signed_divexact_u32_workspace(vm1, 2, &frame->div_temp))) &&
        signed_sub_inplace_workspace(v1, v0, &frame->signed_temp) &&
        signed_sub_inplace_workspace(v2, v1, &frame->signed_temp) &&
        (((interp_flags & XRAY_TOOM3_INTERP_INPLACE_DIV) && (interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2)) ?
          signed_divexact_pow2_inplace_workspace(v2, 1) :
        ((interp_flags & XRAY_TOOM3_INTERP_SHIFT_DIV2) ?
          signed_divexact_pow2_workspace(v2, 1, &frame->div_temp) :
          signed_divexact_u32_workspace(v2, 2, &frame->div_temp))) &&
        signed_sub_inplace_workspace(v1, vm1, &frame->signed_temp) &&
        signed_sub_inplace_workspace(v1, vinf, &frame->signed_temp) &&
        signed_copy(&frame->twice_vinf, vinf) &&
        signed_mul_u32_inplace(&frame->twice_vinf, 2) &&
        signed_sub_inplace_workspace(v2, &frame->twice_vinf, &frame->signed_temp) &&
        signed_sub_inplace_workspace(vm1, v2, &frame->signed_temp);
    }
  }

  if (ok) {
    ok = v0->sign >= 0 && vm1->sign >= 0 && v1->sign >= 0 && v2->sign >= 0 && vinf->sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 4U) &&
      xray_bigint_copy(out, &v0->mag) &&
      add_shifted_inplace_no_normalize(out, &vm1->mag, split) &&
      add_shifted_inplace_no_normalize(out, &v1->mag, split * 2U) &&
      add_shifted_inplace_no_normalize(out, &v2->mag, split * 3U) &&
      add_shifted_inplace_no_normalize(out, &vinf->mag, split * 4U);
    if (ok) normalize(out);
  }
  return ok;
}

static int mul_toom3_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags) {
  if (!out || !left || !right) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace workspace;
  toom3_workspace_init(&workspace);
  int ok = toom3_workspace_prepare(&workspace, max_count, active_depth) &&
    reserve_limbs(out, left_count + right_count + 4U) &&
    mul_toom3_workspace_recurse(out, left, right, active_threshold, 1, active_depth, &workspace, NULL, 0, interp_flags);
  toom3_workspace_clear(&workspace);
  return ok;
}

static int mul_toom3_full_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags) {
  if (!out || !left || !right) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom3_workspace_init(&toom_workspace);
  karatsuba_workspace_init(&karatsuba_workspace);
  int ok = toom3_workspace_prepare(&toom_workspace, max_count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, max_count, active_threshold) &&
    reserve_limbs(out, left_count + right_count + 4U) &&
    mul_toom3_workspace_recurse(out, left, right, active_threshold, 1, active_depth, &toom_workspace, &karatsuba_workspace, 0, interp_flags);
  karatsuba_workspace_clear(&karatsuba_workspace);
  toom3_workspace_clear(&toom_workspace);
  return ok;
}

static int mul_toom3_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *workspace) {
  if (!out || !left || !right || !workspace) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  int ok = toom3_workspace_prepare(&toom_workspace, max_count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, max_count, active_threshold);
  workspace->toom3_frames = toom_workspace.frames;
  workspace->toom3_frame_count = toom_workspace.frame_count;
  workspace->karatsuba_frames = karatsuba_workspace.frames;
  workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  return ok &&
    reserve_limbs(out, left_count + right_count + 4U) &&
    mul_toom3_workspace_recurse(out, left, right, active_threshold, 1, active_depth, &toom_workspace, &karatsuba_workspace, 0, interp_flags);
}

static int square_toom3_full_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  int use_mul_points) {
  if (!out || !value) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom3_workspace_init(&toom_workspace);
  karatsuba_workspace_init(&karatsuba_workspace);
  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold) &&
    reserve_limbs(out, value->count * 2U + 4U) &&
    square_toom3_workspace_recurse(out, value, active_threshold, 1, active_depth, &toom_workspace, &karatsuba_workspace, 0, interp_flags, use_mul_points, 0U, 0U);
  karatsuba_workspace_clear(&karatsuba_workspace);
  toom3_workspace_clear(&toom_workspace);
  return ok;
}

static int square_toom3_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *workspace,
  int use_mul_points,
  size_t parallel_min_count,
  size_t parallel_group_count) {
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold);
  workspace->toom3_frames = toom_workspace.frames;
  workspace->toom3_frame_count = toom_workspace.frame_count;
  workspace->karatsuba_frames = karatsuba_workspace.frames;
  workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  return ok &&
    reserve_limbs(out, value->count * 2U + 4U) &&
    square_toom3_workspace_recurse(
      out,
      value,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags,
      use_mul_points,
      parallel_min_count,
      parallel_group_count);
}

static int square_toom3_direct_coeff_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || value->count < active_threshold * 3U) {
    if (karatsuba_workspace) {
      return square_karatsuba_workspace_recurse(out, value, active_threshold, use_unroll4, 0, karatsuba_workspace, 0);
    }
    return mul_dispatch_threshold_mode_ex(out, value, value, active_threshold, use_unroll4, 1, 1);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;
  if (depth_limit > 1U && depth + 1U >= workspace->frame_count) return 0;

  size_t split = (value->count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, value->count > split * 2U ? value->count - split * 2U : 0);

  XrayScratchBigInt c0, c1, c2, c3, c4, twice_a0a2;
  xray_bigint_init(&c0);
  xray_bigint_init(&c1);
  xray_bigint_init(&c2);
  xray_bigint_init(&c3);
  xray_bigint_init(&c4);
  xray_bigint_init(&twice_a0a2);

  size_t child_depth_limit = depth_limit > 0U ? depth_limit - 1U : 0U;
  int ok = square_toom3_workspace_recurse(
      &c0,
      &a0,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags,
      0,
      0U,
      0U) &&
    square_toom3_workspace_recurse(
      &c2,
      &a1,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags,
      0,
      0U,
      0U) &&
    square_toom3_workspace_recurse(
      &c4,
      &a2,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags,
      0,
      0U,
      0U) &&
    mul_toom3_workspace_recurse(
      &c1,
      &a0,
      &a1,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    mul_toom3_workspace_recurse(
      &twice_a0a2,
      &a0,
      &a2,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    mul_toom3_workspace_recurse(
      &c3,
      &a1,
      &a2,
      active_threshold,
      use_unroll4,
      child_depth_limit,
      workspace,
      karatsuba_workspace,
      depth + 1U,
      interp_flags) &&
    xray_bigint_add(&c1, &c1, &c1) &&
    xray_bigint_add(&twice_a0a2, &twice_a0a2, &twice_a0a2) &&
    xray_bigint_add(&c2, &c2, &twice_a0a2) &&
    xray_bigint_add(&c3, &c3, &c3);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 4U) &&
      add_shifted_inplace(out, &c0, 0) &&
      add_shifted_inplace(out, &c1, split) &&
      add_shifted_inplace(out, &c2, split * 2U) &&
      add_shifted_inplace(out, &c3, split * 3U) &&
      add_shifted_inplace(out, &c4, split * 4U);
    if (ok) normalize(out);
  }

  xray_bigint_clear(&twice_a0a2);
  xray_bigint_clear(&c4);
  xray_bigint_clear(&c3);
  xray_bigint_clear(&c2);
  xray_bigint_clear(&c1);
  xray_bigint_clear(&c0);
  return ok;
}

static int square_toom3_chung_sqr3_workspace_recurse(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit,
  XrayToom3Workspace *workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace,
  size_t depth,
  unsigned int interp_flags) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || value->count < active_threshold * 3U) {
    if (karatsuba_workspace) {
      return square_karatsuba_workspace_recurse(out, value, active_threshold, use_unroll4, 0, karatsuba_workspace, 0);
    }
    return mul_dispatch_threshold_mode_ex(out, value, value, active_threshold, use_unroll4, 1, 1);
  }
  if (!workspace || depth >= workspace->frame_count) return 0;
  if (depth_limit > 1U && depth + 1U >= workspace->frame_count) return 0;

  size_t split = (value->count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, value->count > split * 2U ? value->count - split * 2U : 0);

  XrayToom3WorkspaceFrame *frame = &workspace->frames[depth];
  XraySignedScratchBigInt *c0 = &frame->v[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *s1 = &frame->v[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *s2 = &frame->v[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *c3 = &frame->v[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *c4 = &frame->v[XRAY_TOOM3_POINT_INF];
  XrayScratchBigInt *point1 = &frame->x[XRAY_TOOM3_POINT_1].mag;
  XrayScratchBigInt *point_minus1 = &frame->x[XRAY_TOOM3_POINT_MINUS_1].mag;
  XrayScratchBigInt *t1 = &frame->eval_temp;
  XrayScratchBigInt *c1 = &frame->div_temp;
  XrayScratchBigInt *c2 = &frame->y[XRAY_TOOM3_POINT_0].mag;

  size_t child_depth_limit = depth_limit > 0U ? depth_limit - 1U : 0U;
  int use_direct_child = child_depth_limit == 0U && karatsuba_workspace;
  if (!use_direct_child) toom3_workspace_frame_reset(frame);

  int ok = add3_unsigned(point1, &a0, &a1, &a2);
  ok = ok &&
    eval_toom3_minus_one_abs_sum02_workspace(point_minus1, &a0, &a1, &a2, &frame->signed_temp.mag);

  if (ok && use_direct_child) {
    ok = square_karatsuba_workspace_recurse(&c0->mag, &a0, active_threshold, use_unroll4, 0, karatsuba_workspace, 0) &&
      square_karatsuba_workspace_recurse(&s1->mag, point1, active_threshold, use_unroll4, 0, karatsuba_workspace, 0) &&
      square_karatsuba_workspace_recurse(&s2->mag, point_minus1, active_threshold, use_unroll4, 0, karatsuba_workspace, 0) &&
      mul_karatsuba_workspace_recurse(&c3->mag, &a1, &a2, active_threshold, use_unroll4, karatsuba_workspace, 0) &&
      square_karatsuba_workspace_recurse(&c4->mag, &a2, active_threshold, use_unroll4, 0, karatsuba_workspace, 0);
  } else if (ok) {
    ok = square_toom3_workspace_recurse(
        &c0->mag,
        &a0,
        active_threshold,
        use_unroll4,
        child_depth_limit,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        0,
        0U,
        0U) &&
      square_toom3_workspace_recurse(
        &s1->mag,
        point1,
        active_threshold,
        use_unroll4,
        child_depth_limit,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        0,
        0U,
        0U) &&
      square_toom3_workspace_recurse(
        &s2->mag,
        point_minus1,
        active_threshold,
        use_unroll4,
        child_depth_limit,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        0,
        0U,
        0U) &&
      mul_toom3_workspace_recurse(
        &c3->mag,
        &a1,
        &a2,
        active_threshold,
        use_unroll4,
        child_depth_limit,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags) &&
      square_toom3_workspace_recurse(
        &c4->mag,
        &a2,
        active_threshold,
        use_unroll4,
        child_depth_limit,
        workspace,
        karatsuba_workspace,
        depth + 1U,
        interp_flags,
        0,
        0U,
        0U);
  }

  ok = ok &&
    xray_bigint_add(&c3->mag, &c3->mag, &c3->mag) &&
    xray_bigint_add(t1, &s1->mag, &s2->mag) &&
    shift_right_bits_inplace_exact(t1, 1) &&
    sub_known_greater_or_equal(c1, &s1->mag, t1) &&
    sub_known_greater_or_equal(c1, c1, &c3->mag) &&
    sub_known_greater_or_equal(c2, t1, &c4->mag) &&
    sub_known_greater_or_equal(c2, c2, &c0->mag);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 4U) &&
      xray_bigint_copy(out, &c0->mag) &&
      add_shifted_inplace_no_normalize(out, c1, split) &&
      add_shifted_inplace_no_normalize(out, c2, split * 2U) &&
      add_shifted_inplace_no_normalize(out, &c3->mag, split * 3U) &&
      add_shifted_inplace_no_normalize(out, &c4->mag, split * 4U);
    if (ok) normalize(out);
  }

  return ok;
}

static int square_toom3_direct_coeff_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *workspace) {
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold);
  workspace->toom3_frames = toom_workspace.frames;
  workspace->toom3_frame_count = toom_workspace.frame_count;
  workspace->karatsuba_frames = karatsuba_workspace.frames;
  workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  return ok &&
    reserve_limbs(out, value->count * 2U + 4U) &&
    square_toom3_direct_coeff_workspace_recurse(
      out,
      value,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
}

static int square_toom3_chung_sqr3_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *workspace) {
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold);
  workspace->toom3_frames = toom_workspace.frames;
  workspace->toom3_frame_count = toom_workspace.frame_count;
  workspace->karatsuba_frames = karatsuba_workspace.frames;
  workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  return ok &&
    reserve_limbs(out, value->count * 2U + 4U) &&
    square_toom3_chung_sqr3_workspace_recurse(
      out,
      value,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
}

static int square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t top_leaf_threshold,
  size_t child_leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  size_t parallel_group_count,
  XrayBigIntMulWorkspace *workspace) {
  if (!out || !value || !workspace) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t active_top_threshold = top_leaf_threshold >= 2U ? top_leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_threshold = child_leaf_threshold >= 2U ? child_leaf_threshold : active_top_threshold;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (value->count < active_top_threshold * 4U) return 0;

  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth + 1U) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold);
  workspace->toom3_frames = toom_workspace.frames;
  workspace->toom3_frame_count = toom_workspace.frame_count;
  workspace->karatsuba_frames = karatsuba_workspace.frames;
  workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  if (!ok) return 0;

  size_t split = (value->count + 3U) / 4U;
  XrayScratchBigInt a0, a1, a2, a3;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, split);
  view_bigint_slice(&a3, value, split * 3U, value->count > split * 3U ? value->count - split * 3U : 0);

  XrayToom3WorkspaceFrame *frame = &toom_workspace.frames[active_depth];
  toom3_workspace_frame_reset(frame);
  XraySignedScratchBigInt *s1 = &frame->v[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *s2 = &frame->v[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *s3 = &frame->v[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *s4 = &frame->v[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *s5 = &frame->v[XRAY_TOOM3_POINT_INF];
  XraySignedScratchBigInt *s6 = &frame->x[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *s7 = &frame->x[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *u02 = &frame->x[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *u13 = &frame->x[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *lin_plus = &frame->x[XRAY_TOOM3_POINT_INF];
  XraySignedScratchBigInt *lin_minus = &frame->y[XRAY_TOOM3_POINT_0];
  XraySignedScratchBigInt *sum_all = &frame->y[XRAY_TOOM3_POINT_1];
  XraySignedScratchBigInt *t1 = &frame->y[XRAY_TOOM3_POINT_MINUS_1];
  XraySignedScratchBigInt *t2 = &frame->y[XRAY_TOOM3_POINT_2];
  XraySignedScratchBigInt *t3 = &frame->y[XRAY_TOOM3_POINT_INF];
  XraySignedScratchBigInt *t4 = &frame->twice_vinf;
  XraySignedScratchBigInt *temp = &frame->signed_temp;

  ok = signed_set_diff_unsigned(u02, &a0, &a2) &&
    signed_set_diff_unsigned(u13, &a1, &a3) &&
    signed_add_workspace(lin_plus, u02, u13, temp) &&
    signed_sub_workspace(lin_minus, u02, u13, temp) &&
    add3_unsigned(&sum_all->mag, &a0, &a1, &a2) &&
    xray_bigint_add(&sum_all->mag, &sum_all->mag, &a3);
  sum_all->sign = ok && sum_all->mag.count ? 1 : 0;

  int point_products_ready = 0;
  if (ok && parallel_group_count > 0U) {
#if defined(_WIN32) && defined(_MSC_VER)
    XraySignedScratchBigInt in0, in1, in2, in3;
    in0.sign = a0.count ? 1 : 0;
    in0.mag = a0;
    in1.sign = a1.count ? 1 : 0;
    in1.mag = a1;
    in2.sign = a2.count ? 1 : 0;
    in2.mag = a2;
    in3.sign = a3.count ? 1 : 0;
    in3.mag = a3;
    enum { XRAY_SQR4_POINT_PRODUCT_COUNT = 7 };
    size_t child_depth_limit = active_depth > 0U ? active_depth - 1U : 0U;
    XrayToomPointProductTask tasks[XRAY_SQR4_POINT_PRODUCT_COUNT];
    tasks[0] = (XrayToomPointProductTask){s1, &in0, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
    tasks[1] = (XrayToomPointProductTask){s2, &in0, &in1, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
    tasks[2] = (XrayToomPointProductTask){s3, lin_plus, lin_minus, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
    tasks[3] = (XrayToomPointProductTask){s4, sum_all, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
    tasks[4] = (XrayToomPointProductTask){s5, u02, u13, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
    tasks[5] = (XrayToomPointProductTask){s6, &in3, &in2, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
    tasks[6] = (XrayToomPointProductTask){s7, &in3, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
    point_products_ready = run_toom_point_products_parallel_grouped_persistent(
      tasks,
      XRAY_SQR4_POINT_PRODUCT_COUNT,
      parallel_group_count);
    if (!point_products_ready) {
      point_products_ready = run_toom_point_products_parallel_grouped_pool(
        tasks,
        XRAY_SQR4_POINT_PRODUCT_COUNT,
        parallel_group_count);
    }
    if (point_products_ready) {
      ok = signed_mul_u32_inplace(s2, 2U) &&
        signed_mul_u32_inplace(s5, 2U) &&
        signed_mul_u32_inplace(s6, 2U);
    } else {
      signed_reset(s1);
      signed_reset(s2);
      signed_reset(s3);
      signed_reset(s4);
      signed_reset(s5);
      signed_reset(s6);
      signed_reset(s7);
    }
#else
    point_products_ready = 0;
#endif
  }

  if (ok && !point_products_ready) {
  if (ok) {
    ok = square_toom3_workspace_recurse(
      &s1->mag,
      &a0,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags,
      0,
      0U,
      0U);
    s1->sign = ok && s1->mag.count ? 1 : 0;
  }
  if (ok) {
    ok = mul_toom3_workspace_recurse(
      &s2->mag,
      &a0,
      &a1,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
    s2->sign = ok && s2->mag.count ? 1 : 0;
    if (ok) ok = signed_mul_u32_inplace(s2, 2U);
  }
  if (ok) {
    ok = signed_mul_toom3_workspace_mode(
      s3,
      lin_plus,
      lin_minus,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
  }
  if (ok) {
    ok = square_toom3_workspace_recurse(
      &s4->mag,
      &sum_all->mag,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags,
      0,
      0U,
      0U);
    s4->sign = ok && s4->mag.count ? 1 : 0;
  }
  if (ok) {
    ok = signed_mul_toom3_workspace_mode(
      s5,
      u02,
      u13,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
    if (ok) ok = signed_mul_u32_inplace(s5, 2U);
  }
  if (ok) {
    ok = mul_toom3_workspace_recurse(
      &s6->mag,
      &a3,
      &a2,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags);
    s6->sign = ok && s6->mag.count ? 1 : 0;
    if (ok) ok = signed_mul_u32_inplace(s6, 2U);
  }
  if (ok) {
    ok = square_toom3_workspace_recurse(
      &s7->mag,
      &a3,
      active_threshold,
      1,
      active_depth,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      interp_flags,
      0,
      0U,
      0U);
    s7->sign = ok && s7->mag.count ? 1 : 0;
  }
  }

  XraySignedScratchBigInt *c3 = s3;
  XraySignedScratchBigInt *c2 = sum_all;
  XraySignedScratchBigInt *c4 = lin_minus;
  XraySignedScratchBigInt *t6 = lin_plus;
  ok = ok &&
    signed_add_workspace(t1, s3, s4, temp) &&
    signed_add_workspace(t2, t1, s5, temp) &&
    signed_divexact_pow2_inplace_workspace(t2, 1U) &&
    signed_add_workspace(t3, s2, s6, temp) &&
    signed_sub_workspace(t4, t2, t3, temp) &&
    signed_sub_workspace(t6, t4, s3, temp) &&
    signed_sub_workspace(c4, t4, s1, temp) &&
    signed_sub_workspace(c2, t6, s7, temp) &&
    signed_sub_workspace(c3, t3, s5, temp);

  if (ok) {
    ok = s1->sign >= 0 && s2->sign >= 0 && c2->sign >= 0 && c3->sign >= 0 &&
      c4->sign >= 0 && s6->sign >= 0 && s7->sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 8U) &&
      xray_bigint_copy(out, &s1->mag) &&
      add_shifted_inplace_no_normalize(out, &s2->mag, split) &&
      add_shifted_inplace_no_normalize(out, &c2->mag, split * 2U) &&
      add_shifted_inplace_no_normalize(out, &c3->mag, split * 3U) &&
      add_shifted_inplace_no_normalize(out, &c4->mag, split * 4U) &&
      add_shifted_inplace_no_normalize(out, &s6->mag, split * 5U) &&
      add_shifted_inplace_no_normalize(out, &s7->mag, split * 6U);
    if (ok) normalize(out);
  }

  return ok;
}

static int square_toom3_direct_coeff_parallel_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *workspace,
  size_t parallel_group_count) {
  if (!out || !value || !workspace) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (active_depth == 0 || value->count < active_threshold * 3U) {
    XrayKaratsubaWorkspace karatsuba_workspace;
    karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
    karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
    int ok = karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold) &&
      square_karatsuba_workspace_recurse(out, value, active_threshold, 1, 0, &karatsuba_workspace, 0);
    workspace->karatsuba_frames = karatsuba_workspace.frames;
    workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
    return ok;
  }

#if defined(_WIN32) && defined(_MSC_VER)
  size_t split = (value->count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, value->count > split * 2U ? value->count - split * 2U : 0);

  XraySignedScratchBigInt in0, in1, in2;
  in0.sign = a0.count ? 1 : 0;
  in0.mag = a0;
  in1.sign = a1.count ? 1 : 0;
  in1.mag = a1;
  in2.sign = a2.count ? 1 : 0;
  in2.mag = a2;

  enum { XRAY_DIRECT_COEFF_TERM_COUNT = 6 };
  XraySignedScratchBigInt terms[XRAY_DIRECT_COEFF_TERM_COUNT];
  for (size_t index = 0; index < XRAY_DIRECT_COEFF_TERM_COUNT; ++index) signed_init(&terms[index]);

  size_t child_depth_limit = active_depth > 0U ? active_depth - 1U : 0U;
  XrayToomPointProductTask tasks[XRAY_DIRECT_COEFF_TERM_COUNT];
  tasks[0] = (XrayToomPointProductTask){&terms[0], &in0, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
  tasks[1] = (XrayToomPointProductTask){&terms[1], &in1, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
  tasks[2] = (XrayToomPointProductTask){&terms[2], &in2, NULL, value->count, active_threshold, child_depth_limit, interp_flags, 1, 0};
  tasks[3] = (XrayToomPointProductTask){&terms[3], &in0, &in1, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
  tasks[4] = (XrayToomPointProductTask){&terms[4], &in0, &in2, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};
  tasks[5] = (XrayToomPointProductTask){&terms[5], &in1, &in2, value->count, active_threshold, child_depth_limit, interp_flags, 0, 0};

  size_t group_count = parallel_group_count > 0U ? parallel_group_count : 5U;
  int ok = run_toom_point_products_parallel_grouped_persistent(tasks, XRAY_DIRECT_COEFF_TERM_COUNT, group_count);
  if (!ok) {
    ok = run_toom_point_products_parallel_grouped_pool(tasks, XRAY_DIRECT_COEFF_TERM_COUNT, group_count);
  }
  for (size_t index = 0; ok && index < XRAY_DIRECT_COEFF_TERM_COUNT; ++index) {
    ok = terms[index].sign >= 0;
  }
  ok = ok &&
    xray_bigint_add(&terms[3].mag, &terms[3].mag, &terms[3].mag) &&
    xray_bigint_add(&terms[4].mag, &terms[4].mag, &terms[4].mag) &&
    xray_bigint_add(&terms[1].mag, &terms[1].mag, &terms[4].mag) &&
    xray_bigint_add(&terms[5].mag, &terms[5].mag, &terms[5].mag);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 4U) &&
      add_shifted_inplace(out, &terms[0].mag, 0) &&
      add_shifted_inplace(out, &terms[3].mag, split) &&
      add_shifted_inplace(out, &terms[1].mag, split * 2U) &&
      add_shifted_inplace(out, &terms[5].mag, split * 3U) &&
      add_shifted_inplace(out, &terms[2].mag, split * 4U);
    if (ok) normalize(out);
  }

  for (size_t index = XRAY_DIRECT_COEFF_TERM_COUNT; index-- > 0;) signed_clear(&terms[index]);
  return ok;
#else
  (void)interp_flags;
  (void)parallel_group_count;
  return 0;
#endif
}

static int eval_toom4_positive(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3,
  uint64_t weight1,
  uint64_t weight2,
  uint64_t weight3) {
  return set_u32(out, 0) &&
    add_scaled_unsigned(out, part0, 1) &&
    add_scaled_unsigned(out, part1, weight1) &&
    add_scaled_unsigned(out, part2, weight2) &&
    add_scaled_unsigned(out, part3, weight3);
}

static int eval_toom4_half_scaled(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3) {
  return set_u32(out, 0) &&
    add_scaled_unsigned(out, part0, 8) &&
    add_scaled_unsigned(out, part1, 4) &&
    add_scaled_unsigned(out, part2, 2) &&
    add_scaled_unsigned(out, part3, 1);
}

static int signed_set_toom4_half_scaled(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3) {
  if (!out || !part0 || !part1 || !part2 || !part3) return 0;
  int ok = eval_toom4_half_scaled(&out->mag, part0, part1, part2, part3);
  out->sign = ok && out->mag.count ? 1 : 0;
  return ok;
}

static int signed_set_toom4_eval(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3,
  int point) {
  if (!out || !part0 || !part1 || !part2 || !part3) return 0;
  if (point == 0) return signed_set_unsigned(out, part0);
  if (point > 0) {
    uint64_t x = (uint64_t)point;
    XrayScratchBigInt eval;
    xray_bigint_init(&eval);
    int ok = eval_toom4_positive(&eval, part0, part1, part2, part3, x, x * x, x * x * x) &&
      signed_set_unsigned(out, &eval);
    xray_bigint_clear(&eval);
    return ok;
  }

  uint64_t x = (uint64_t)(-point);
  XrayScratchBigInt even;
  XrayScratchBigInt odd;
  xray_bigint_init(&even);
  xray_bigint_init(&odd);
  int ok = set_u32(&even, 0) &&
    add_scaled_unsigned(&even, part0, 1) &&
    add_scaled_unsigned(&even, part2, x * x) &&
    set_u32(&odd, 0) &&
    add_scaled_unsigned(&odd, part1, x) &&
    add_scaled_unsigned(&odd, part3, x * x * x);
  if (ok) {
    int compare = xray_bigint_compare(&even, &odd);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, &even, &odd);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, &odd, &even);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  xray_bigint_clear(&even);
  xray_bigint_clear(&odd);
  return ok;
}

static int signed_scaled_copy(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *value,
  uint32_t scale) {
  if (!out || !value) return 0;
  int ok = signed_copy(out, value);
  if (ok && scale != 1U) ok = signed_mul_u32_inplace(out, scale);
  return ok;
}

static int signed_toom4_adjust_value(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *value,
  const XraySignedScratchBigInt *v0,
  const XraySignedScratchBigInt *vinf,
  uint32_t vinf_scale,
  XraySignedScratchBigInt *scaled,
  XraySignedScratchBigInt *temp) {
  if (!out || !value || !v0 || !vinf || !scaled || !temp) return 0;
  return signed_copy(out, value) &&
    signed_sub_inplace_workspace(out, v0, temp) &&
    signed_scaled_copy(scaled, vinf, vinf_scale) &&
    signed_sub_inplace_workspace(out, scaled, temp);
}

static int signed_linear_combination_divexact_workspace(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt **terms,
  const int *coefficients,
  size_t term_count,
  uint32_t divisor,
  XraySignedScratchBigInt *scaled,
  XraySignedScratchBigInt *temp,
  XrayScratchBigInt *quotient,
  unsigned int interp_flags) {
  if (!out || !terms || !coefficients || !scaled || !temp || !quotient || divisor == 0) return 0;
  signed_reset(out);
  for (size_t index = 0; index < term_count; ++index) {
    int coefficient = coefficients[index];
    if (coefficient == 0) continue;
    uint32_t magnitude = coefficient < 0 ? (uint32_t)(-coefficient) : (uint32_t)coefficient;
    int ok = signed_scaled_copy(scaled, terms[index], magnitude);
    if (ok && coefficient < 0) scaled->sign = -scaled->sign;
    if (!ok || !signed_add_workspace(out, out, scaled, temp)) return 0;
  }
  if (interp_flags & XRAY_TOOM4_INTERP_FACTORED_DIV) {
    if (divisor == 24U) {
      return signed_divexact_pow2_workspace(out, 3, quotient) &&
        signed_divexact_u3_workspace(out, quotient);
    }
    if (divisor == 60U) {
      return signed_divexact_pow2_workspace(out, 2, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient);
    }
    if (divisor == 120U) {
      return signed_divexact_pow2_workspace(out, 3, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient);
    }
    if (divisor == 18U) {
      return signed_divexact_pow2_workspace(out, 1, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient);
    }
    if (divisor == 180U) {
      return signed_divexact_pow2_workspace(out, 2, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient);
    }
    if (divisor == 144U) {
      return signed_divexact_pow2_workspace(out, 4, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient);
    }
    if (divisor == 360U) {
      return signed_divexact_pow2_workspace(out, 3, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient);
    }
    if (divisor == 420U) {
      return signed_divexact_pow2_workspace(out, 2, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient) &&
        signed_divexact_u32_workspace(out, 7U, quotient);
    }
    if (divisor == 720U) {
      return signed_divexact_pow2_workspace(out, 4, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient);
    }
    if (divisor == 5040U) {
      return signed_divexact_pow2_workspace(out, 4, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u3_workspace(out, quotient) &&
        signed_divexact_u5_workspace(out, quotient) &&
        signed_divexact_u32_workspace(out, 7U, quotient);
    }
  }
  return signed_divexact_u32_workspace(out, divisor, quotient);
}

static int run_toom_point_product_task(XrayToomPointProductTask *task) {
  if (!task || !task->out || !task->left || (!task->square_point && !task->right)) return 0;
  if (task->left->sign == 0 || (!task->square_point && task->right->sign == 0)) {
    task->out->sign = 0;
    return set_u32(&task->out->mag, 0);
  }
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom3_workspace_init(&toom_workspace);
  karatsuba_workspace_init(&karatsuba_workspace);
  int ok = toom3_workspace_prepare(&toom_workspace, task->workspace_count, task->depth_limit) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, task->workspace_count, task->threshold);
  if (ok && task->square_point) {
    ok = square_toom3_workspace_recurse(
      &task->out->mag,
      &task->left->mag,
      task->threshold,
      1,
      task->depth_limit,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      task->interp_flags,
      0,
      0U,
      0U);
    task->out->sign = ok && task->out->mag.count ? 1 : 0;
  } else if (ok) {
    ok = mul_toom3_workspace_recurse(
      &task->out->mag,
      &task->left->mag,
      &task->right->mag,
      task->threshold,
      1,
      task->depth_limit,
      &toom_workspace,
      &karatsuba_workspace,
      0,
      task->interp_flags);
    task->out->sign = ok && task->out->mag.count ? task->left->sign * task->right->sign : 0;
  }
  karatsuba_workspace_clear(&karatsuba_workspace);
  toom3_workspace_clear(&toom_workspace);
  return ok;
}

#if defined(_WIN32) && defined(_MSC_VER)
static unsigned __stdcall run_toom_point_product_thread(void *context) {
  XrayToomPointProductTask *task = (XrayToomPointProductTask *)context;
  task->ok = run_toom_point_product_task(task);
  return 0U;
}

typedef struct XrayToomPointProductGroupTask {
  XrayToomPointProductTask *tasks;
  size_t start;
  size_t end;
  int ok;
} XrayToomPointProductGroupTask;

static int run_toom_point_product_group_task_with_workspace(
  XrayToomPointProductGroupTask *group,
  XrayToom3Workspace *toom_workspace,
  XrayKaratsubaWorkspace *karatsuba_workspace) {
  if (!group || !group->tasks || group->start >= group->end || !toom_workspace || !karatsuba_workspace) return 0;
  XrayToomPointProductTask *first = &group->tasks[group->start];
  int ok = toom3_workspace_prepare(toom_workspace, first->workspace_count, first->depth_limit) &&
    karatsuba_workspace_prepare(karatsuba_workspace, first->workspace_count, first->threshold);
  for (size_t index = group->start; ok && index < group->end; ++index) {
    XrayToomPointProductTask *task = &group->tasks[index];
    if (task->left->sign == 0 || (!task->square_point && task->right->sign == 0)) {
      task->out->sign = 0;
      ok = set_u32(&task->out->mag, 0);
    } else if (task->square_point) {
      ok = square_toom3_workspace_recurse(
        &task->out->mag,
        &task->left->mag,
        task->threshold,
        1,
        task->depth_limit,
        toom_workspace,
        karatsuba_workspace,
        0,
        task->interp_flags,
        0,
        0U,
        0U);
      task->out->sign = ok && task->out->mag.count ? 1 : 0;
    } else {
      ok = mul_toom3_workspace_recurse(
        &task->out->mag,
        &task->left->mag,
        &task->right->mag,
        task->threshold,
        1,
        task->depth_limit,
        toom_workspace,
        karatsuba_workspace,
        0,
        task->interp_flags);
      task->out->sign = ok && task->out->mag.count ? task->left->sign * task->right->sign : 0;
    }
    task->ok = ok;
  }
  return ok;
}

static int run_toom_point_product_group_task(XrayToomPointProductGroupTask *group) {
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom3_workspace_init(&toom_workspace);
  karatsuba_workspace_init(&karatsuba_workspace);
  int ok = run_toom_point_product_group_task_with_workspace(group, &toom_workspace, &karatsuba_workspace);
  karatsuba_workspace_clear(&karatsuba_workspace);
  toom3_workspace_clear(&toom_workspace);
  return ok;
}

static unsigned __stdcall run_toom_point_product_group_thread(void *context) {
  XrayToomPointProductGroupTask *group = (XrayToomPointProductGroupTask *)context;
  group->ok = run_toom_point_product_group_task(group);
  return 0U;
}

static void __stdcall run_toom_point_product_group_pool(
  XrayWinThreadpoolInstance instance,
  void *context,
  XrayWinThreadpoolWork work) {
  (void)instance;
  (void)work;
  XrayToomPointProductGroupTask *group = (XrayToomPointProductGroupTask *)context;
  group->ok = run_toom_point_product_group_task(group);
}

static int run_toom_point_products_parallel(XrayToomPointProductTask *tasks, size_t task_count) {
  if (!tasks || task_count == 0 || task_count > XRAY_WIN_MAXIMUM_WAIT_OBJECTS) return 0;
  XrayWinHandle handles[XRAY_WIN_MAXIMUM_WAIT_OBJECTS];
  size_t started = 0;
  for (size_t index = 0; index < task_count; ++index) {
    tasks[index].ok = 0;
    uintptr_t handle = _beginthreadex(NULL, 0, run_toom_point_product_thread, &tasks[index], 0, NULL);
    if (!handle) break;
    handles[started++] = (XrayWinHandle)handle;
  }
  if (started > 0) {
    WaitForMultipleObjects((unsigned long)started, handles, 1, XRAY_WIN_WAIT_INFINITE);
    for (size_t index = 0; index < started; ++index) CloseHandle(handles[index]);
  }
  if (started != task_count) return 0;
  int ok = 1;
  for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
  return ok;
}

static int run_toom_point_products_parallel_grouped_pool(
  XrayToomPointProductTask *tasks,
  size_t task_count,
  size_t group_count) {
  if (!tasks || task_count == 0) return 0;
  if (group_count == 0 || group_count > task_count) group_count = task_count;
  if (group_count > XRAY_WIN_MAXIMUM_WAIT_OBJECTS) return 0;
  XrayToomPointProductGroupTask groups[XRAY_WIN_MAXIMUM_WAIT_OBJECTS];
  XrayWinThreadpoolWork works[XRAY_WIN_MAXIMUM_WAIT_OBJECTS];
  size_t created = 0;
  for (size_t group_index = 0; group_index < group_count; ++group_index) {
    groups[group_index].tasks = tasks;
    groups[group_index].start = (task_count * group_index) / group_count;
    groups[group_index].end = (task_count * (group_index + 1U)) / group_count;
    groups[group_index].ok = 0;
    XrayWinThreadpoolWork work = CreateThreadpoolWork(
      run_toom_point_product_group_pool,
      &groups[group_index],
      NULL);
    if (!work) break;
    works[created++] = work;
  }
  if (created != group_count) {
    for (size_t index = 0; index < created; ++index) CloseThreadpoolWork(works[index]);
    return 0;
  }
  for (size_t index = 0; index < created; ++index) SubmitThreadpoolWork(works[index]);
  for (size_t index = 0; index < created; ++index) {
    WaitForThreadpoolWorkCallbacks(works[index], 0);
    CloseThreadpoolWork(works[index]);
  }
  int ok = 1;
  for (size_t group_index = 0; group_index < group_count; ++group_index) ok = ok && groups[group_index].ok;
  for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
  return ok;
}

typedef struct XrayToomPointWorker {
  XrayWinHandle thread;
  XrayWinHandle start_event;
  XrayWinHandle done_event;
  XrayToomPointProductGroupTask *group;
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  int workspaces_initialized;
  volatile long stop;
} XrayToomPointWorker;

typedef struct XrayToomPointWorkerPool {
  XrayToomPointWorker workers[XRAY_TOOM_POINT_POOL_WORKERS];
  int initialized;
  int atexit_registered;
} XrayToomPointWorkerPool;

static XRAY_BIGINT_THREAD_LOCAL XrayToomPointWorkerPool toom_point_tls_pool;

static unsigned __stdcall run_toom_point_worker_thread(void *context) {
  XrayToomPointWorker *worker = (XrayToomPointWorker *)context;
  if (!worker) return 0U;
  for (;;) {
    WaitForSingleObject(worker->start_event, XRAY_WIN_WAIT_INFINITE);
    if (worker->stop) {
      SetEvent(worker->done_event);
      return 0U;
    }
    if (worker->group) {
      worker->group->ok = run_toom_point_product_group_task_with_workspace(
        worker->group,
        &worker->toom_workspace,
        &worker->karatsuba_workspace);
    }
    SetEvent(worker->done_event);
  }
}

static void toom_point_worker_pool_clear(XrayToomPointWorkerPool *pool) {
  if (!pool) return;
  XrayWinHandle threads[XRAY_TOOM_POINT_POOL_WORKERS];
  size_t thread_count = 0;
  for (size_t index = 0; index < XRAY_TOOM_POINT_POOL_WORKERS; ++index) {
    XrayToomPointWorker *worker = &pool->workers[index];
    if (worker->thread && worker->start_event && worker->done_event) {
      worker->stop = 1;
      ResetEvent(worker->done_event);
      SetEvent(worker->start_event);
      threads[thread_count++] = worker->thread;
    }
  }
  if (thread_count) WaitForMultipleObjects((unsigned long)thread_count, threads, 1, XRAY_WIN_WAIT_INFINITE);
  for (size_t index = 0; index < XRAY_TOOM_POINT_POOL_WORKERS; ++index) {
    XrayToomPointWorker *worker = &pool->workers[index];
    if (worker->thread) CloseHandle(worker->thread);
    if (worker->start_event) CloseHandle(worker->start_event);
    if (worker->done_event) CloseHandle(worker->done_event);
    if (worker->workspaces_initialized) {
      karatsuba_workspace_clear(&worker->karatsuba_workspace);
      toom3_workspace_clear(&worker->toom_workspace);
    }
    memset(worker, 0, sizeof(*worker));
  }
  pool->initialized = 0;
}

static void toom_point_worker_pool_clear_at_exit(void) {
  toom_point_worker_pool_clear(&toom_point_tls_pool);
}

static int toom_point_worker_pool_prepare(XrayToomPointWorkerPool *pool) {
  if (!pool) return 0;
  if (pool->initialized) return 1;
  memset(pool->workers, 0, sizeof(pool->workers));
  for (size_t index = 0; index < XRAY_TOOM_POINT_POOL_WORKERS; ++index) {
    XrayToomPointWorker *worker = &pool->workers[index];
    toom3_workspace_init(&worker->toom_workspace);
    karatsuba_workspace_init(&worker->karatsuba_workspace);
    worker->workspaces_initialized = 1;
    worker->start_event = CreateEventA(NULL, 0, 0, NULL);
    worker->done_event = CreateEventA(NULL, 1, 0, NULL);
    if (!worker->start_event || !worker->done_event) {
      toom_point_worker_pool_clear(pool);
      return 0;
    }
    uintptr_t handle = _beginthreadex(NULL, 0, run_toom_point_worker_thread, worker, 0, NULL);
    if (!handle) {
      toom_point_worker_pool_clear(pool);
      return 0;
    }
    worker->thread = (XrayWinHandle)handle;
  }
  pool->initialized = 1;
  if (!pool->atexit_registered) {
    (void)atexit(toom_point_worker_pool_clear_at_exit);
    pool->atexit_registered = 1;
  }
  return 1;
}

static int run_toom_point_products_parallel_grouped_persistent(
  XrayToomPointProductTask *tasks,
  size_t task_count,
  size_t group_count) {
  if (!tasks || task_count == 0) return 0;
  if (group_count == 0 || group_count > task_count) group_count = task_count;
  if (group_count > XRAY_TOOM_POINT_POOL_WORKERS) return 0;
  XrayToomPointWorkerPool *pool = &toom_point_tls_pool;
  if (!toom_point_worker_pool_prepare(pool)) return 0;
  XrayToomPointProductGroupTask groups[XRAY_TOOM_POINT_POOL_WORKERS];
  XrayWinHandle done_events[XRAY_TOOM_POINT_POOL_WORKERS];
  for (size_t group_index = 0; group_index < group_count; ++group_index) {
    groups[group_index].tasks = tasks;
    groups[group_index].start = (task_count * group_index) / group_count;
    groups[group_index].end = (task_count * (group_index + 1U)) / group_count;
    groups[group_index].ok = 0;
    XrayToomPointWorker *worker = &pool->workers[group_index];
    worker->group = &groups[group_index];
    ResetEvent(worker->done_event);
    done_events[group_index] = worker->done_event;
    SetEvent(worker->start_event);
  }
  WaitForMultipleObjects((unsigned long)group_count, done_events, 1, XRAY_WIN_WAIT_INFINITE);
  int ok = 1;
  for (size_t group_index = 0; group_index < group_count; ++group_index) {
    pool->workers[group_index].group = NULL;
    ok = ok && groups[group_index].ok;
  }
  for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
  return ok;
}

static int run_toom_point_products_parallel_grouped(
  XrayToomPointProductTask *tasks,
  size_t task_count,
  size_t group_count) {
  if (!tasks || task_count == 0) return 0;
  if (group_count == 0 || group_count > task_count) group_count = task_count;
  if (group_count > XRAY_WIN_MAXIMUM_WAIT_OBJECTS) return 0;
  XrayToomPointProductGroupTask groups[XRAY_WIN_MAXIMUM_WAIT_OBJECTS];
  XrayWinHandle handles[XRAY_WIN_MAXIMUM_WAIT_OBJECTS];
  size_t started = 0;
  for (size_t group_index = 0; group_index < group_count; ++group_index) {
    groups[group_index].tasks = tasks;
    groups[group_index].start = (task_count * group_index) / group_count;
    groups[group_index].end = (task_count * (group_index + 1U)) / group_count;
    groups[group_index].ok = 0;
    uintptr_t handle = _beginthreadex(NULL, 0, run_toom_point_product_group_thread, &groups[group_index], 0, NULL);
    if (!handle) break;
    handles[started++] = (XrayWinHandle)handle;
  }
  if (started > 0) {
    WaitForMultipleObjects((unsigned long)started, handles, 1, XRAY_WIN_WAIT_INFINITE);
    for (size_t index = 0; index < started; ++index) CloseHandle(handles[index]);
  }
  if (started != group_count) return 0;
  int ok = 1;
  for (size_t group_index = 0; group_index < group_count; ++group_index) ok = ok && groups[group_index].ok;
  for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
  return ok;
}
#endif

static int mul_toom4_top_full_workspace_probe_internal_ex(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *reuse_workspace,
  size_t parallel_min_count,
  size_t parallel_group_count) {
  if (!out || !left || !right) return 0;
  if (reuse_workspace &&
      (reuse_workspace == (const XrayBigIntMulWorkspace *)out ||
       reuse_workspace == (const XrayBigIntMulWorkspace *)left ||
       reuse_workspace == (const XrayBigIntMulWorkspace *)right)) {
    return 0;
  }
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (max_count < active_threshold * 4U || min_count * 4U < max_count * 3U) {
    return reuse_workspace ?
      mul_toom3_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, reuse_workspace) :
      mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags);
  }

  size_t split = (max_count + 3U) / 4U;
  XrayScratchBigInt a0, a1, a2, a3;
  XrayScratchBigInt b0, b1, b2, b3;
  view_bigint_slice(&a0, left, 0, split);
  view_bigint_slice(&a1, left, split, split);
  view_bigint_slice(&a2, left, split * 2U, split);
  view_bigint_slice(&a3, left, split * 3U, left_count > split * 3U ? left_count - split * 3U : 0);
  view_bigint_slice(&b0, right, 0, split);
  view_bigint_slice(&b1, right, split, split);
  view_bigint_slice(&b2, right, split * 2U, split);
  view_bigint_slice(&b3, right, split * 3U, right_count > split * 3U ? right_count - split * 3U : 0);

  enum { T4_0 = 0, T4_1, T4_M1, T4_2, T4_M2, T4_HALF, T4_INF, T4_COUNT };
  int use_parallel_points = 0;
#if defined(_WIN32) && defined(_MSC_VER)
  use_parallel_points = parallel_min_count > 0U && max_count >= parallel_min_count;
#endif
  XraySignedScratchBigInt x[T4_COUNT];
  XraySignedScratchBigInt y[T4_COUNT];
  XraySignedScratchBigInt v[T4_COUNT];
  XraySignedScratchBigInt s1, sm1, s2, sm2, s3;
  XraySignedScratchBigInt coeff[7];
  XraySignedScratchBigInt scaled, temp;
  XrayScratchBigInt quotient;
  for (size_t index = 0; index < T4_COUNT; ++index) {
    signed_init(&x[index]);
    signed_init(&y[index]);
    signed_init(&v[index]);
  }
  signed_init(&s1);
  signed_init(&sm1);
  signed_init(&s2);
  signed_init(&sm2);
  signed_init(&s3);
  for (size_t index = 0; index < 7U; ++index) signed_init(&coeff[index]);
  signed_init(&scaled);
  signed_init(&temp);
  xray_bigint_init(&quotient);

  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  int owns_workspace = reuse_workspace == NULL;
  if (reuse_workspace) {
    toom_workspace.frames = (XrayToom3WorkspaceFrame *)reuse_workspace->toom3_frames;
    toom_workspace.frame_count = reuse_workspace->toom3_frame_count;
    karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)reuse_workspace->karatsuba_frames;
    karatsuba_workspace.frame_count = reuse_workspace->karatsuba_frame_count;
  } else {
    toom3_workspace_init(&toom_workspace);
    karatsuba_workspace_init(&karatsuba_workspace);
  }
  int ok = (use_parallel_points ||
      (toom3_workspace_prepare(&toom_workspace, max_count, active_depth) &&
       karatsuba_workspace_prepare(&karatsuba_workspace, max_count, active_threshold))) &&
    signed_set_toom4_eval(&x[T4_0], &a0, &a1, &a2, &a3, 0) &&
    signed_set_toom4_eval(&x[T4_1], &a0, &a1, &a2, &a3, 1) &&
    signed_set_toom4_eval(&x[T4_M1], &a0, &a1, &a2, &a3, -1) &&
    signed_set_toom4_eval(&x[T4_2], &a0, &a1, &a2, &a3, 2) &&
    signed_set_toom4_eval(&x[T4_M2], &a0, &a1, &a2, &a3, -2) &&
    signed_set_toom4_half_scaled(&x[T4_HALF], &a0, &a1, &a2, &a3) &&
    signed_set_unsigned(&x[T4_INF], &a3) &&
    signed_set_toom4_eval(&y[T4_0], &b0, &b1, &b2, &b3, 0) &&
    signed_set_toom4_eval(&y[T4_1], &b0, &b1, &b2, &b3, 1) &&
    signed_set_toom4_eval(&y[T4_M1], &b0, &b1, &b2, &b3, -1) &&
    signed_set_toom4_eval(&y[T4_2], &b0, &b1, &b2, &b3, 2) &&
    signed_set_toom4_eval(&y[T4_M2], &b0, &b1, &b2, &b3, -2) &&
    signed_set_toom4_half_scaled(&y[T4_HALF], &b0, &b1, &b2, &b3) &&
    signed_set_unsigned(&y[T4_INF], &b3);
  if (reuse_workspace && !use_parallel_points) {
    reuse_workspace->toom3_frames = toom_workspace.frames;
    reuse_workspace->toom3_frame_count = toom_workspace.frame_count;
    reuse_workspace->karatsuba_frames = karatsuba_workspace.frames;
    reuse_workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  }

  int point_products_ready = 0;
  if (ok && use_parallel_points) {
#if defined(_WIN32) && defined(_MSC_VER)
    XrayToomPointProductTask tasks[T4_COUNT];
    for (size_t index = 0; index < T4_COUNT; ++index) {
      tasks[index].out = &v[index];
      tasks[index].left = &x[index];
      tasks[index].right = &y[index];
      tasks[index].workspace_count = max_count;
      tasks[index].threshold = active_threshold;
      tasks[index].depth_limit = active_depth;
      tasks[index].interp_flags = interp_flags;
      tasks[index].square_point = 0;
      tasks[index].ok = 0;
    }
    size_t point_group_count = parallel_group_count > 0U ?
      parallel_group_count :
      (max_count >= 3200U ? 4U : 3U);
    point_products_ready = run_toom_point_products_parallel_grouped_persistent(tasks, T4_COUNT, point_group_count);
    if (!point_products_ready) {
      point_products_ready = run_toom_point_products_parallel_grouped_pool(tasks, T4_COUNT, point_group_count);
    }
    if (!point_products_ready) {
      for (size_t index = 0; index < T4_COUNT; ++index) signed_reset(&v[index]);
    }
#else
    point_products_ready = 0;
#endif
  }
  if (ok && !point_products_ready) {
    if (use_parallel_points) {
      ok = toom3_workspace_prepare(&toom_workspace, max_count, active_depth) &&
        karatsuba_workspace_prepare(&karatsuba_workspace, max_count, active_threshold);
      if (reuse_workspace && ok) {
        reuse_workspace->toom3_frames = toom_workspace.frames;
        reuse_workspace->toom3_frame_count = toom_workspace.frame_count;
        reuse_workspace->karatsuba_frames = karatsuba_workspace.frames;
        reuse_workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
      }
    }
    for (size_t index = 0; ok && index < T4_COUNT; ++index) {
      if (x[index].sign == 0 || y[index].sign == 0) {
        ok = set_u32(&v[index].mag, 0);
        v[index].sign = 0;
      } else {
        ok = mul_toom3_workspace_recurse(
          &v[index].mag,
          &x[index].mag,
          &y[index].mag,
          active_threshold,
          1,
          active_depth,
          &toom_workspace,
          &karatsuba_workspace,
          0,
          interp_flags);
        v[index].sign = ok && v[index].mag.count ? x[index].sign * y[index].sign : 0;
      }
    }
  }

  if (ok) {
    const XraySignedScratchBigInt *terms[5] = {&s1, &sm1, &s2, &sm2, &s3};
    static const int c1_coeffs[5] = {-120, -40, 5, 3, 8};
    static const int c2_coeffs[5] = {16, 16, -1, -1, 0};
    static const int c3_coeffs[5] = {27, -7, -1, 0, -1};
    static const int c4_coeffs[5] = {-4, -4, 1, 1, 0};
    static const int c5_coeffs[5] = {-60, 20, 5, -3, 2};
    ok = signed_toom4_adjust_value(&s1, &v[T4_1], &v[T4_0], &v[T4_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&sm1, &v[T4_M1], &v[T4_0], &v[T4_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&s2, &v[T4_2], &v[T4_0], &v[T4_INF], 64U, &scaled, &temp) &&
      signed_toom4_adjust_value(&sm2, &v[T4_M2], &v[T4_0], &v[T4_INF], 64U, &scaled, &temp) &&
      signed_copy(&s3, &v[T4_HALF]) &&
      signed_scaled_copy(&scaled, &v[T4_0], 64U) &&
      signed_sub_inplace_workspace(&s3, &scaled, &temp) &&
      signed_sub_inplace_workspace(&s3, &v[T4_INF], &temp) &&
      signed_copy(&coeff[0], &v[T4_0]) &&
      signed_linear_combination_divexact_workspace(&coeff[1], terms, c1_coeffs, 5U, 180U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[2], terms, c2_coeffs, 5U, 24U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[3], terms, c3_coeffs, 5U, 18U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[4], terms, c4_coeffs, 5U, 24U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[5], terms, c5_coeffs, 5U, 180U, &scaled, &temp, &quotient, interp_flags) &&
      signed_copy(&coeff[6], &v[T4_INF]);
  }

  if (ok) {
    for (size_t index = 0; index < 7U; ++index) ok = ok && coeff[index].sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count + 8U);
    for (size_t index = 0; ok && index < 7U; ++index) {
      ok = add_shifted_inplace(out, &coeff[index].mag, split * index);
    }
    if (ok) normalize(out);
  }

  if (owns_workspace) {
    karatsuba_workspace_clear(&karatsuba_workspace);
    toom3_workspace_clear(&toom_workspace);
  }
  xray_bigint_clear(&quotient);
  signed_clear(&scaled);
  signed_clear(&temp);
  for (size_t index = 0; index < 7U; ++index) signed_clear(&coeff[index]);
  signed_clear(&s1);
  signed_clear(&sm1);
  signed_clear(&s2);
  signed_clear(&sm2);
  signed_clear(&s3);
  for (size_t index = 0; index < T4_COUNT; ++index) {
    signed_clear(&x[index]);
    signed_clear(&y[index]);
    signed_clear(&v[index]);
  }
  return ok;
}

static int mul_toom4_top_full_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *reuse_workspace) {
  return mul_toom4_top_full_workspace_probe_internal_ex(
    out,
    left,
    right,
    leaf_threshold,
    depth_limit,
    interp_flags,
    reuse_workspace,
    1800U,
    0U);
}

static int square_toom4_top_full_workspace_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *reuse_workspace,
  int use_mul_points) {
  if (!out || !value) return 0;
  if (reuse_workspace &&
      (reuse_workspace == (const XrayBigIntMulWorkspace *)out ||
       reuse_workspace == (const XrayBigIntMulWorkspace *)value)) {
    return 0;
  }
  if (value->count == 0) return set_u32(out, 0);
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (value->count < active_threshold * 4U) {
    if (use_mul_points) {
      return reuse_workspace ?
        mul_toom3_full_workspace_reuse_probe_internal(out, value, value, active_threshold, active_depth, interp_flags, reuse_workspace) :
        mul_toom3_full_workspace_probe_internal(out, value, value, active_threshold, active_depth, interp_flags);
    }
    return reuse_workspace ?
      square_toom3_full_workspace_reuse_probe_internal(out, value, active_threshold, active_depth, interp_flags, reuse_workspace, 0, 0U, 0U) :
      square_toom3_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, 0);
  }

  size_t split = (value->count + 3U) / 4U;
  XrayScratchBigInt a0, a1, a2, a3;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, split);
  view_bigint_slice(&a3, value, split * 3U, value->count > split * 3U ? value->count - split * 3U : 0);

  enum { T4_0 = 0, T4_1, T4_M1, T4_2, T4_M2, T4_HALF, T4_INF, T4_COUNT };
  XraySignedScratchBigInt x[T4_COUNT];
  XraySignedScratchBigInt v[T4_COUNT];
  XraySignedScratchBigInt s1, sm1, s2, sm2, s3;
  XraySignedScratchBigInt coeff[7];
  XraySignedScratchBigInt scaled, temp;
  XrayScratchBigInt quotient;
  for (size_t index = 0; index < T4_COUNT; ++index) {
    signed_init(&x[index]);
    signed_init(&v[index]);
  }
  signed_init(&s1);
  signed_init(&sm1);
  signed_init(&s2);
  signed_init(&sm2);
  signed_init(&s3);
  for (size_t index = 0; index < 7U; ++index) signed_init(&coeff[index]);
  signed_init(&scaled);
  signed_init(&temp);
  xray_bigint_init(&quotient);

  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  int owns_workspace = reuse_workspace == NULL;
  if (reuse_workspace) {
    toom_workspace.frames = (XrayToom3WorkspaceFrame *)reuse_workspace->toom3_frames;
    toom_workspace.frame_count = reuse_workspace->toom3_frame_count;
    karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)reuse_workspace->karatsuba_frames;
    karatsuba_workspace.frame_count = reuse_workspace->karatsuba_frame_count;
  } else {
    toom3_workspace_init(&toom_workspace);
    karatsuba_workspace_init(&karatsuba_workspace);
  }

  int ok = toom3_workspace_prepare(&toom_workspace, value->count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, value->count, active_threshold) &&
    signed_set_toom4_eval(&x[T4_0], &a0, &a1, &a2, &a3, 0) &&
    signed_set_toom4_eval(&x[T4_1], &a0, &a1, &a2, &a3, 1) &&
    signed_set_toom4_eval(&x[T4_M1], &a0, &a1, &a2, &a3, -1) &&
    signed_set_toom4_eval(&x[T4_2], &a0, &a1, &a2, &a3, 2) &&
    signed_set_toom4_eval(&x[T4_M2], &a0, &a1, &a2, &a3, -2) &&
    signed_set_toom4_half_scaled(&x[T4_HALF], &a0, &a1, &a2, &a3) &&
    signed_set_unsigned(&x[T4_INF], &a3);
  if (reuse_workspace) {
    reuse_workspace->toom3_frames = toom_workspace.frames;
    reuse_workspace->toom3_frame_count = toom_workspace.frame_count;
    reuse_workspace->karatsuba_frames = karatsuba_workspace.frames;
    reuse_workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;
  }

  for (size_t index = 0; ok && index < T4_COUNT; ++index) {
    if (x[index].sign == 0) {
      ok = set_u32(&v[index].mag, 0);
      v[index].sign = 0;
    } else {
      ok = use_mul_points ?
        mul_toom3_workspace_recurse(
          &v[index].mag,
          &x[index].mag,
          &x[index].mag,
          active_threshold,
          1,
          active_depth,
          &toom_workspace,
          &karatsuba_workspace,
          0,
          interp_flags) :
        square_toom3_workspace_recurse(
          &v[index].mag,
          &x[index].mag,
          active_threshold,
          1,
          active_depth,
          &toom_workspace,
          &karatsuba_workspace,
          0,
          interp_flags,
          0,
          0U,
          0U);
      v[index].sign = ok && v[index].mag.count ? 1 : 0;
    }
  }

  if (ok) {
    const XraySignedScratchBigInt *terms[5] = {&s1, &sm1, &s2, &sm2, &s3};
    static const int c1_coeffs[5] = {-120, -40, 5, 3, 8};
    static const int c2_coeffs[5] = {16, 16, -1, -1, 0};
    static const int c3_coeffs[5] = {27, -7, -1, 0, -1};
    static const int c4_coeffs[5] = {-4, -4, 1, 1, 0};
    static const int c5_coeffs[5] = {-60, 20, 5, -3, 2};
    ok = signed_toom4_adjust_value(&s1, &v[T4_1], &v[T4_0], &v[T4_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&sm1, &v[T4_M1], &v[T4_0], &v[T4_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&s2, &v[T4_2], &v[T4_0], &v[T4_INF], 64U, &scaled, &temp) &&
      signed_toom4_adjust_value(&sm2, &v[T4_M2], &v[T4_0], &v[T4_INF], 64U, &scaled, &temp) &&
      signed_copy(&s3, &v[T4_HALF]) &&
      signed_scaled_copy(&scaled, &v[T4_0], 64U) &&
      signed_sub_inplace_workspace(&s3, &scaled, &temp) &&
      signed_sub_inplace_workspace(&s3, &v[T4_INF], &temp) &&
      signed_copy(&coeff[0], &v[T4_0]) &&
      signed_linear_combination_divexact_workspace(&coeff[1], terms, c1_coeffs, 5U, 180U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[2], terms, c2_coeffs, 5U, 24U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[3], terms, c3_coeffs, 5U, 18U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[4], terms, c4_coeffs, 5U, 24U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[5], terms, c5_coeffs, 5U, 180U, &scaled, &temp, &quotient, interp_flags) &&
      signed_copy(&coeff[6], &v[T4_INF]);
  }

  if (ok) {
    for (size_t index = 0; index < 7U; ++index) ok = ok && coeff[index].sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 8U);
    for (size_t index = 0; ok && index < 7U; ++index) {
      ok = add_shifted_inplace(out, &coeff[index].mag, split * index);
    }
    if (ok) normalize(out);
  }

  if (owns_workspace) {
    karatsuba_workspace_clear(&karatsuba_workspace);
    toom3_workspace_clear(&toom_workspace);
  }
  xray_bigint_clear(&quotient);
  signed_clear(&scaled);
  signed_clear(&temp);
  for (size_t index = 0; index < 7U; ++index) signed_clear(&coeff[index]);
  signed_clear(&s1);
  signed_clear(&sm1);
  signed_clear(&s2);
  signed_clear(&sm2);
  signed_clear(&s3);
  for (size_t index = 0; index < T4_COUNT; ++index) {
    signed_clear(&x[index]);
    signed_clear(&v[index]);
  }
  return ok;
}

static int eval_toom5_positive(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3,
  const XrayScratchBigInt *part4,
  uint64_t weight1,
  uint64_t weight2,
  uint64_t weight3,
  uint64_t weight4) {
  return set_u32(out, 0) &&
    add_scaled_unsigned(out, part0, 1) &&
    add_scaled_unsigned(out, part1, weight1) &&
    add_scaled_unsigned(out, part2, weight2) &&
    add_scaled_unsigned(out, part3, weight3) &&
    add_scaled_unsigned(out, part4, weight4);
}

static int signed_set_toom5_eval(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  const XrayScratchBigInt *part3,
  const XrayScratchBigInt *part4,
  int point) {
  if (!out || !part0 || !part1 || !part2 || !part3 || !part4) return 0;
  if (point == 0) return signed_set_unsigned(out, part0);
  if (point > 0) {
    uint64_t x = (uint64_t)point;
    XrayScratchBigInt eval;
    xray_bigint_init(&eval);
    int ok = eval_toom5_positive(&eval, part0, part1, part2, part3, part4, x, x * x, x * x * x, x * x * x * x) &&
      signed_set_unsigned(out, &eval);
    xray_bigint_clear(&eval);
    return ok;
  }

  uint64_t x = (uint64_t)(-point);
  XrayScratchBigInt even;
  XrayScratchBigInt odd;
  xray_bigint_init(&even);
  xray_bigint_init(&odd);
  int ok = set_u32(&even, 0) &&
    add_scaled_unsigned(&even, part0, 1) &&
    add_scaled_unsigned(&even, part2, x * x) &&
    add_scaled_unsigned(&even, part4, x * x * x * x) &&
    set_u32(&odd, 0) &&
    add_scaled_unsigned(&odd, part1, x) &&
    add_scaled_unsigned(&odd, part3, x * x * x);
  if (ok) {
    int compare = xray_bigint_compare(&even, &odd);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, &even, &odd);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, &odd, &even);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  xray_bigint_clear(&even);
  xray_bigint_clear(&odd);
  return ok;
}

static int mul_toom5_top_full_workspace_reuse_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  unsigned int interp_flags,
  XrayBigIntMulWorkspace *reuse_workspace) {
  if (!out || !left || !right || !reuse_workspace) return 0;
  if (reuse_workspace == (const XrayBigIntMulWorkspace *)out ||
      reuse_workspace == (const XrayBigIntMulWorkspace *)left ||
      reuse_workspace == (const XrayBigIntMulWorkspace *)right) {
    return 0;
  }
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (max_count < active_threshold * 5U || min_count * 5U < max_count * 4U) {
    return mul_toom3_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, reuse_workspace);
  }

  size_t split = (max_count + 4U) / 5U;
  XrayScratchBigInt a0, a1, a2, a3, a4;
  XrayScratchBigInt b0, b1, b2, b3, b4;
  view_bigint_slice(&a0, left, 0, split);
  view_bigint_slice(&a1, left, split, split);
  view_bigint_slice(&a2, left, split * 2U, split);
  view_bigint_slice(&a3, left, split * 3U, split);
  view_bigint_slice(&a4, left, split * 4U, left_count > split * 4U ? left_count - split * 4U : 0);
  view_bigint_slice(&b0, right, 0, split);
  view_bigint_slice(&b1, right, split, split);
  view_bigint_slice(&b2, right, split * 2U, split);
  view_bigint_slice(&b3, right, split * 3U, split);
  view_bigint_slice(&b4, right, split * 4U, right_count > split * 4U ? right_count - split * 4U : 0);

  enum { T5_0 = 0, T5_1, T5_M1, T5_2, T5_M2, T5_3, T5_M3, T5_4, T5_INF, T5_COUNT };
  XraySignedScratchBigInt x[T5_COUNT];
  XraySignedScratchBigInt y[T5_COUNT];
  XraySignedScratchBigInt v[T5_COUNT];
  XraySignedScratchBigInt adjusted[7];
  XraySignedScratchBigInt coeff[9];
  XraySignedScratchBigInt scaled, temp;
  XrayScratchBigInt quotient;
  for (size_t index = 0; index < T5_COUNT; ++index) {
    signed_init(&x[index]);
    signed_init(&y[index]);
    signed_init(&v[index]);
  }
  for (size_t index = 0; index < 7U; ++index) signed_init(&adjusted[index]);
  for (size_t index = 0; index < 9U; ++index) signed_init(&coeff[index]);
  signed_init(&scaled);
  signed_init(&temp);
  xray_bigint_init(&quotient);

  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)reuse_workspace->toom3_frames;
  toom_workspace.frame_count = reuse_workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)reuse_workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = reuse_workspace->karatsuba_frame_count;

  int ok = toom3_workspace_prepare(&toom_workspace, max_count, active_depth) &&
    karatsuba_workspace_prepare(&karatsuba_workspace, max_count, active_threshold) &&
    signed_set_toom5_eval(&x[T5_0], &a0, &a1, &a2, &a3, &a4, 0) &&
    signed_set_toom5_eval(&x[T5_1], &a0, &a1, &a2, &a3, &a4, 1) &&
    signed_set_toom5_eval(&x[T5_M1], &a0, &a1, &a2, &a3, &a4, -1) &&
    signed_set_toom5_eval(&x[T5_2], &a0, &a1, &a2, &a3, &a4, 2) &&
    signed_set_toom5_eval(&x[T5_M2], &a0, &a1, &a2, &a3, &a4, -2) &&
    signed_set_toom5_eval(&x[T5_3], &a0, &a1, &a2, &a3, &a4, 3) &&
    signed_set_toom5_eval(&x[T5_M3], &a0, &a1, &a2, &a3, &a4, -3) &&
    signed_set_toom5_eval(&x[T5_4], &a0, &a1, &a2, &a3, &a4, 4) &&
    signed_set_unsigned(&x[T5_INF], &a4) &&
    signed_set_toom5_eval(&y[T5_0], &b0, &b1, &b2, &b3, &b4, 0) &&
    signed_set_toom5_eval(&y[T5_1], &b0, &b1, &b2, &b3, &b4, 1) &&
    signed_set_toom5_eval(&y[T5_M1], &b0, &b1, &b2, &b3, &b4, -1) &&
    signed_set_toom5_eval(&y[T5_2], &b0, &b1, &b2, &b3, &b4, 2) &&
    signed_set_toom5_eval(&y[T5_M2], &b0, &b1, &b2, &b3, &b4, -2) &&
    signed_set_toom5_eval(&y[T5_3], &b0, &b1, &b2, &b3, &b4, 3) &&
    signed_set_toom5_eval(&y[T5_M3], &b0, &b1, &b2, &b3, &b4, -3) &&
    signed_set_toom5_eval(&y[T5_4], &b0, &b1, &b2, &b3, &b4, 4) &&
    signed_set_unsigned(&y[T5_INF], &b4);
  reuse_workspace->toom3_frames = toom_workspace.frames;
  reuse_workspace->toom3_frame_count = toom_workspace.frame_count;
  reuse_workspace->karatsuba_frames = karatsuba_workspace.frames;
  reuse_workspace->karatsuba_frame_count = karatsuba_workspace.frame_count;

  for (size_t index = 0; ok && index < T5_COUNT; ++index) {
    if (x[index].sign == 0 || y[index].sign == 0) {
      ok = set_u32(&v[index].mag, 0);
      v[index].sign = 0;
    } else {
      ok = mul_toom3_workspace_recurse(
        &v[index].mag,
        &x[index].mag,
        &y[index].mag,
        active_threshold,
        1,
        active_depth,
        &toom_workspace,
        &karatsuba_workspace,
        0,
        interp_flags);
      v[index].sign = ok && v[index].mag.count ? x[index].sign * y[index].sign : 0;
    }
  }

  if (ok) {
    const XraySignedScratchBigInt *terms[7] = {
      &adjusted[0], &adjusted[1], &adjusted[2], &adjusted[3],
      &adjusted[4], &adjusted[5], &adjusted[6]
    };
    static const int c1_coeffs[7] = {420, -252, -126, 42, 28, -4, -3};
    static const int c2_coeffs[7] = {270, 270, -27, -27, 2, 2, 0};
    static const int c3_coeffs[7] = {-440, 48, 267, -71, -64, 8, 7};
    static const int c4_coeffs[7] = {-39, -39, 12, 12, -1, -1, 0};
    static const int c5_coeffs[7] = {85, 27, -54, -2, 17, -1, -2};
    static const int c6_coeffs[7] = {15, 15, -6, -6, 1, 1, 0};
    static const int c7_coeffs[7] = {-35, -21, 21, 7, -7, -1, 1};
    ok = signed_toom4_adjust_value(&adjusted[0], &v[T5_1], &v[T5_0], &v[T5_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[1], &v[T5_M1], &v[T5_0], &v[T5_INF], 1U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[2], &v[T5_2], &v[T5_0], &v[T5_INF], 256U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[3], &v[T5_M2], &v[T5_0], &v[T5_INF], 256U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[4], &v[T5_3], &v[T5_0], &v[T5_INF], 6561U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[5], &v[T5_M3], &v[T5_0], &v[T5_INF], 6561U, &scaled, &temp) &&
      signed_toom4_adjust_value(&adjusted[6], &v[T5_4], &v[T5_0], &v[T5_INF], 65536U, &scaled, &temp) &&
      signed_copy(&coeff[0], &v[T5_0]) &&
      signed_linear_combination_divexact_workspace(&coeff[1], terms, c1_coeffs, 7U, 420U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[2], terms, c2_coeffs, 7U, 360U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[3], terms, c3_coeffs, 7U, 720U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[4], terms, c4_coeffs, 7U, 144U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[5], terms, c5_coeffs, 7U, 720U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[6], terms, c6_coeffs, 7U, 720U, &scaled, &temp, &quotient, interp_flags) &&
      signed_linear_combination_divexact_workspace(&coeff[7], terms, c7_coeffs, 7U, 5040U, &scaled, &temp, &quotient, interp_flags) &&
      signed_copy(&coeff[8], &v[T5_INF]);
  }

  if (ok) {
    for (size_t index = 0; index < 9U; ++index) ok = ok && coeff[index].sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count + 10U);
    for (size_t index = 0; ok && index < 9U; ++index) {
      ok = add_shifted_inplace(out, &coeff[index].mag, split * index);
    }
    if (ok) normalize(out);
  }

  xray_bigint_clear(&quotient);
  signed_clear(&scaled);
  signed_clear(&temp);
  for (size_t index = 0; index < 9U; ++index) signed_clear(&coeff[index]);
  for (size_t index = 0; index < 7U; ++index) signed_clear(&adjusted[index]);
  for (size_t index = 0; index < T5_COUNT; ++index) {
    signed_clear(&x[index]);
    signed_clear(&y[index]);
    signed_clear(&v[index]);
  }
  return ok;
}
#endif

#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS && XRAY_BIGINT_HAS_THREAD_LOCAL
static XRAY_BIGINT_THREAD_LOCAL XrayBigIntMulWorkspace dense_toom4_tls_workspace;
static XRAY_BIGINT_THREAD_LOCAL int dense_toom4_tls_workspace_initialized;
static XRAY_BIGINT_THREAD_LOCAL int dense_toom4_tls_workspace_in_use;

static void dense_toom4_tls_workspace_clear_at_exit(void) {
  if (dense_toom4_tls_workspace_initialized && !dense_toom4_tls_workspace_in_use) {
    xray_bigint_mul_workspace_clear(&dense_toom4_tls_workspace);
    dense_toom4_tls_workspace_initialized = 0;
  }
}

static XrayBigIntMulWorkspace *dense_toom4_tls_workspace_acquire(int *using_tls) {
  if (using_tls) *using_tls = 0;
  if (dense_toom4_tls_workspace_in_use) return NULL;
  if (!dense_toom4_tls_workspace_initialized) {
    xray_bigint_mul_workspace_init(&dense_toom4_tls_workspace);
    dense_toom4_tls_workspace_initialized = 1;
    (void)atexit(dense_toom4_tls_workspace_clear_at_exit);
  }
  dense_toom4_tls_workspace_in_use = 1;
  if (using_tls) *using_tls = 1;
  return &dense_toom4_tls_workspace;
}

static void dense_toom4_tls_workspace_release(void) {
  dense_toom4_tls_workspace_in_use = 0;
}
#endif

static int try_dense_toom4_l64d2_dispatch_route(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || left->count == 0 || right->count == 0) return 0;
  size_t left_count = left->count;
  size_t right_count = right->count;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  if (min_count < XRAY_BIGINT_DENSE_TOOM4_L64D2_MIN_LIMBS ||
      max_count > XRAY_BIGINT_DENSE_TOOM4_L64D2_MAX_LIMBS ||
      min_count * 4U < max_count * 3U) {
    return 0;
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  int ok = 0;
  if (max_count < XRAY_BIGINT_DENSE_TOOM4_TOP_MIN_LIMBS) {
    unsigned int combo_interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
      XRAY_TOOM3_INTERP_EXACT_DIV3;
    const int use_upper_combo = max_count >= XRAY_BIGINT_DENSE_TOOM3_COMBO_UPPER_MIN_LIMBS;
    ok = mul_toom3_full_workspace_reuse_probe_internal(
      out,
      left,
      right,
      use_upper_combo ? 40U : 64U,
      use_upper_combo ? 3U : 2U,
      combo_interp_flags,
      workspace);
  } else if (max_count < XRAY_BIGINT_DENSE_TOOM4_PARALLEL_LOW_MAX_LIMBS) {
    ok = mul_toom4_top_full_workspace_probe_internal_ex(
      out,
      left,
      right,
      48U,
      3U,
      interp_flags,
      workspace,
      XRAY_BIGINT_DENSE_TOOM4_TOP_MIN_LIMBS,
      XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_GROUPS);
  } else if (max_count < XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_MAX_LIMBS) {
    ok = mul_toom4_top_full_workspace_probe_internal_ex(
      out,
      left,
      right,
      64U,
      2U,
      interp_flags,
      workspace,
      XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_MIN_LIMBS,
      XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_GROUPS);
  } else if (max_count >= XRAY_BIGINT_DENSE_TOOM4_PARALLEL_UPPER_MIN_LIMBS &&
             max_count < XRAY_BIGINT_DENSE_TOOM4_PARALLEL_UPPER_MAX_LIMBS) {
    ok = mul_toom4_top_full_workspace_probe_internal_ex(
      out,
      left,
      right,
      64U,
      2U,
      interp_flags,
      workspace,
      XRAY_BIGINT_DENSE_TOOM4_PARALLEL_UPPER_MIN_LIMBS,
      XRAY_BIGINT_DENSE_TOOM4_PARALLEL_MID_GROUPS);
  } else {
    ok = mul_toom4_top_full_workspace_probe_internal(
      out,
      left,
      right,
      64U,
      2U,
      interp_flags,
      workspace);
  }
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)left;
  (void)right;
  return 0;
#endif
}

static int mul_dispatch(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (max_count < XRAY_BIGINT_SPARSE_MUL_MIN_LIMBS) {
    int use_unroll4 = min_count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS &&
        max_count <= XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS &&
        min_count * 3U >= max_count * 2U;
    return mul_schoolbook_mode(out, left, right, use_unroll4, 0);
  }
#endif
  if (try_sparse_mul_dispatch_route(out, left, right)) return 1;
  if (min_count >= XRAY_BIGINT_DENSE_NTT16_MIN_LIMBS &&
      xray_bigint_mul_ntt32_probe(out, left, right)) {
    return 1;
  }
  if (min_count >= XRAY_BIGINT_DENSE_NTT16_MIN_LIMBS &&
      xray_bigint_mul_ntt16_probe(out, left, right)) {
    return 1;
  }
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (try_dense_toom4_l64d2_dispatch_route(out, left, right)) return 1;
  if (min_count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS &&
      max_count <= XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS &&
      min_count * 3U >= max_count * 2U) {
    return mul_dispatch_threshold_mode(out, left, right, XRAY_BIGINT_KARATSUBA_THRESHOLD, 1);
  }
#endif
  return mul_dispatch_threshold(out, left, right, XRAY_BIGINT_KARATSUBA_THRESHOLD);
}

static int try_dense_square_chung_sqr4_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  int in_low_window =
    value->count >= XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_MIN_LIMBS &&
    value->count < XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_MAX_LIMBS;
  int in_8192_window =
    value->count >= XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_MIN_LIMBS &&
    value->count < XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_MAX_LIMBS;
  if (!in_low_window && !in_8192_window) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  size_t leaf_threshold = in_8192_window ?
    XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_LEAF_LIMBS :
    XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_LEAF_LIMBS;
  size_t depth_limit = in_8192_window ?
    XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_DEPTH :
    XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_DEPTH;
  size_t parallel_group_count = in_8192_window ?
    XRAY_BIGINT_DENSE_SQUARE_CHUNG_SQR4_8192_GROUPS :
    0U;
  int ok = square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
    out,
    value,
    leaf_threshold,
    leaf_threshold,
    depth_limit,
    interp_flags,
    parallel_group_count,
    workspace);
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int try_dense_square_toom4_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  if (value->count < XRAY_BIGINT_DENSE_SQUARE_TOOM4_MIN_LIMBS ||
      value->count > XRAY_BIGINT_DENSE_TOOM4_L64D2_MAX_LIMBS) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  size_t square_leaf_threshold = value->count >= 3200U ? 80U : 48U;
  size_t square_depth_limit = value->count >= 3200U ? 2U : 3U;
  int ok = mul_toom4_top_full_workspace_probe_internal(
    out,
    value,
    value,
    square_leaf_threshold,
    square_depth_limit,
    interp_flags,
    workspace);
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int try_dense_square_self_mul_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  if (value->count < XRAY_BIGINT_DENSE_TOOM4_L64D2_MIN_LIMBS ||
      value->count >= XRAY_BIGINT_DENSE_SQUARE_TOOM4_MIN_LIMBS ||
      value->count > XRAY_BIGINT_DENSE_TOOM4_L64D2_MAX_LIMBS) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  return try_dense_toom4_l64d2_dispatch_route(out, value, value);
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int try_dense_square_toom3_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  if (value->count < XRAY_BIGINT_DENSE_SQUARE_TOOM3_MIN_LIMBS ||
      value->count >= XRAY_BIGINT_DENSE_SQUARE_TOOM3_MAX_LIMBS) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  size_t leaf_threshold =
    value->count < XRAY_BIGINT_DENSE_SQUARE_TOOM3_LOW_LEAF_CUT_LIMBS ? 48U :
    (value->count < XRAY_BIGINT_DENSE_SQUARE_TOOM3_MID_LEAF_CUT_LIMBS ? 64U :
    (value->count >= XRAY_BIGINT_DENSE_SQUARE_TOOM3_UPPER_LEAF_CUT_LIMBS ? 48U : 64U));
  size_t depth_limit =
    value->count < XRAY_BIGINT_DENSE_SQUARE_TOOM3_LOW_LEAF_CUT_LIMBS ? 3U :
    (value->count >= XRAY_BIGINT_DENSE_SQUARE_TOOM3_UPPER_LEAF_CUT_LIMBS ? 3U : 2U);
  int ok = square_toom3_full_workspace_reuse_probe_internal(
    out,
    value,
    leaf_threshold,
    depth_limit,
    interp_flags,
    workspace,
    0,
    0U,
    0U);
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int try_dense_square_parallel_toom3_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  if (value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MIN_LIMBS ||
      value->count >= XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MAX_LIMBS) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  size_t leaf_threshold =
    (value->count >= XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_MIN_LIMBS &&
     value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_MAX_LIMBS) ?
      XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_8192_LEAF_LIMBS :
    value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_LOW_LEAF_CUT_LIMBS ? 48U :
    (value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MID_LEAF_CUT_LIMBS ? 40U :
    (value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_UPPER_LEAF_CUT_LIMBS ? 64U : 48U));
  size_t depth_limit = value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_UPPER_LEAF_CUT_LIMBS ? 2U : 3U;
  int ok = square_toom3_full_workspace_reuse_probe_internal(
    out,
    value,
    leaf_threshold,
    depth_limit,
    interp_flags,
    workspace,
    0,
    1U,
    XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_GROUPS);
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int try_dense_square_parallel_toom4_dispatch_route(XrayScratchBigInt *out, const XrayScratchBigInt *value, int check_sparse) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || value->count == 0) return 0;
  if (value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_MIN_LIMBS ||
      value->count >= XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_MAX_LIMBS ||
      value->count > XRAY_BIGINT_DENSE_TOOM4_L64D2_MAX_LIMBS) {
    return 0;
  }
  if (check_sparse) {
    size_t sparse_cap = value->count / XRAY_BIGINT_SPARSE_SQUARE_DENSITY_DIVISOR;
    size_t nonzero_count = 0;
    if (sparse_cap > 0 && count_nonzero_limbs_bounded(value, sparse_cap, &nonzero_count)) {
      return 0;
    }
  }
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  int using_tls = 0;
  XrayBigIntMulWorkspace local_workspace;
  XrayBigIntMulWorkspace *workspace = NULL;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  workspace = dense_toom4_tls_workspace_acquire(&using_tls);
#endif
  if (!workspace) {
    xray_bigint_mul_workspace_init(&local_workspace);
    workspace = &local_workspace;
  }
  int ok = mul_toom4_top_full_workspace_probe_internal_ex(
    out,
    value,
    value,
    80U,
    2U,
    interp_flags,
    workspace,
    XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_MIN_LIMBS,
    XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM4_GROUPS);
  if (using_tls) {
#if XRAY_BIGINT_HAS_THREAD_LOCAL
    dense_toom4_tls_workspace_release();
#endif
  } else {
    xray_bigint_mul_workspace_clear(&local_workspace);
  }
  return ok;
#else
  (void)out;
  (void)value;
  (void)check_sparse;
  return 0;
#endif
}

static int square_dispatch(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (value && value->count < XRAY_BIGINT_SPARSE_SQUARE_MIN_LIMBS) {
    if (value->count == 0) return set_u32(out, 0);
    if (value->count <= 4U) return square_tiny_dense(out, value);
    if (value->count <= XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
      int use_unroll4 = value->count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
#else
      int use_unroll4 = 0;
#endif
      return mul_schoolbook_mode(out, value, value, use_unroll4, 0);
    }
    return square_schoolbook(out, value);
  }
  if (try_sparse_square_dispatch_route(out, value)) return 1;
  if (value && value->count < XRAY_BIGINT_KARATSUBA_THRESHOLD) {
    return square_schoolbook_dense(out, value);
  }
  if (value &&
      value->count >= XRAY_BIGINT_DENSE_SQUARE_NTT16_MIN_LIMBS &&
      xray_bigint_square_ntt32_lowtailmap_probe(out, value)) {
    return 1;
  }
  if (value &&
      value->count >= XRAY_BIGINT_DENSE_SQUARE_NTT16_MIN_LIMBS &&
      xray_bigint_square_ntt16_probe(out, value)) {
    return 1;
  }
  if (try_dense_square_chung_sqr4_dispatch_route(out, value, 0)) return 1;
  if (value &&
      value->count >= XRAY_BIGINT_DENSE_SQUARE_TOOM3_MIN_LIMBS &&
      value->count < XRAY_BIGINT_DENSE_SQUARE_PARALLEL_TOOM3_MIN_LIMBS &&
      try_dense_square_toom3_dispatch_route(out, value, 0)) {
    return 1;
  }
  if (try_dense_square_toom4_dispatch_route(out, value, 0)) return 1;
  if (try_dense_square_parallel_toom3_dispatch_route(out, value, 0)) return 1;
  if (try_dense_square_parallel_toom4_dispatch_route(out, value, 0)) return 1;
  if (try_dense_square_toom3_dispatch_route(out, value, 0)) return 1;
  if (try_dense_square_self_mul_dispatch_route(out, value, 0)) return 1;
  return square_dispatch_threshold(out, value, XRAY_BIGINT_KARATSUBA_THRESHOLD);
}

int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left == right) return xray_bigint_square(out, left);
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch(&temp, left, right);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch(out, left, right);
}

int xray_bigint_square(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch(&temp, value);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch(out, value);
}

typedef int (*XrayBigintBinaryOp)(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);

static char *decimal_binary_result(const char *left_decimal, const char *right_decimal, XrayBigintBinaryOp op) {
  if (!op) return NULL;
  XrayScratchBigInt left;
  XrayScratchBigInt right;
  XrayScratchBigInt out;
  xray_bigint_init(&left);
  xray_bigint_init(&right);
  xray_bigint_init(&out);

  char *result = NULL;
  if (xray_bigint_set_decimal(&left, left_decimal) &&
      xray_bigint_set_decimal(&right, right_decimal) &&
      op(&out, &left, &right)) {
    result = xray_bigint_get_decimal(&out);
  }

  xray_bigint_clear(&left);
  xray_bigint_clear(&right);
  xray_bigint_clear(&out);
  return result;
}

char *xray_bigint_add_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_add);
}

char *xray_bigint_sub_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_sub);
}

char *xray_bigint_mul_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_mul);
}

char *xray_bigint_square_decimal(const char *decimal) {
  XrayScratchBigInt value;
  XrayScratchBigInt out;
  xray_bigint_init(&value);
  xray_bigint_init(&out);

  char *result = NULL;
  if (xray_bigint_set_decimal(&value, decimal) && xray_bigint_square(&out, &value)) {
    result = xray_bigint_get_decimal(&out);
  }

  xray_bigint_clear(&value);
  xray_bigint_clear(&out);
  return result;
}

int xray_bigint_compare_decimal(const char *left_decimal, const char *right_decimal, int *comparison) {
  if (!comparison) return 0;
  XrayScratchBigInt left;
  XrayScratchBigInt right;
  xray_bigint_init(&left);
  xray_bigint_init(&right);

  int ok = 0;
  if (xray_bigint_set_decimal(&left, left_decimal) &&
      xray_bigint_set_decimal(&right, right_decimal)) {
    *comparison = xray_bigint_compare(&left, &right);
    ok = 1;
  }

  xray_bigint_clear(&left);
  xray_bigint_clear(&right);
  return ok;
}

int xray_bigint_u32_mod_context_init(XrayBigIntU32ModContext *context, uint32_t modulus) {
  if (!context || modulus == 0) return 0;
  context->modulus = modulus;
  context->reciprocal = reciprocal_u32(modulus);
  context->use_fermat_65537 = modulus == XRAY_BIGINT_FERMAT_65537;
  return 1;
}

int xray_bigint_square_karatsuba_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold(&temp, value, active_threshold);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold(out, value, active_threshold);
}

int xray_bigint_square_fused_leaf_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold_mode(&temp, value, active_threshold, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold_mode(out, value, active_threshold, 1);
}

int xray_bigint_square_unroll4_leaf_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold_mode(&temp, value, active_threshold, 2);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold_mode(out, value, active_threshold, 2);
}

int xray_bigint_square_comba_leaf_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold_mode(&temp, value, active_threshold, 3);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold_mode(out, value, active_threshold, 3);
}

int xray_bigint_square_karatsuba_workspace_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_karatsuba_workspace_probe_internal(&temp, value, active_threshold, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_karatsuba_workspace_probe_internal(out, value, active_threshold, 0);
}

int xray_bigint_square_karatsuba_workspace_mul_leaf_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_karatsuba_workspace_probe_internal(&temp, value, active_threshold, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_karatsuba_workspace_probe_internal(out, value, active_threshold, 1);
}

int xray_bigint_mul_with_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold(&temp, left, right, active_threshold);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold(out, left, right, active_threshold);
}

static uint32_t ntt16_mod_pow(uint32_t base, uint32_t exponent, uint32_t modulus) {
  uint64_t result = 1U;
  uint64_t factor = base % modulus;
  while (exponent) {
    if (exponent & 1U) result = (result * factor) % modulus;
    factor = (factor * factor) % modulus;
    exponent >>= 1U;
  }
  return (uint32_t)result;
}

static uint32_t ntt16_shoup_factor(uint32_t factor, uint32_t modulus) {
  return (uint32_t)(((uint64_t)factor << 32U) / modulus);
}

static uint32_t ntt16_mul_shoup(uint32_t value, uint32_t factor, uint32_t factor_shoup, uint32_t modulus) {
  uint64_t quotient = ((uint64_t)value * factor_shoup) >> 32U;
  uint64_t reduced = (uint64_t)value * factor - quotient * modulus;
  while (reduced >= modulus) reduced -= modulus;
  return (uint32_t)reduced;
}

typedef struct XrayNtt16TwiddleCache {
  size_t length;
  uint32_t modulus;
  int inverse;
  uint32_t *twiddles;
  uint32_t *twiddle_shoup;
} XrayNtt16TwiddleCache;

static XrayNtt16TwiddleCache ntt16_twiddle_caches[6];

static int ntt16_twiddle_cache_slot(uint32_t modulus, int inverse) {
  int direction = inverse ? 1 : 0;
  if (modulus == XRAY_BIGINT_NTT16_MOD0) return direction;
  if (modulus == XRAY_BIGINT_NTT16_MOD1) return 2 + direction;
  if (modulus == XRAY_BIGINT_NTT32_MOD2) return 4 + direction;
  return -1;
}

static int ntt16_prepare_twiddle_cache(size_t length, uint32_t modulus, int inverse) {
  if (length < 32768U) return 1;
  int slot = ntt16_twiddle_cache_slot(modulus, inverse);
  if (slot < 0) return 1;
  XrayNtt16TwiddleCache *cache = &ntt16_twiddle_caches[slot];
  if (cache->length == length &&
      cache->modulus == modulus &&
      cache->inverse == (inverse ? 1 : 0) &&
      cache->twiddles &&
      cache->twiddle_shoup) {
    return 1;
  }

  uint32_t *twiddles = (uint32_t *)malloc(sizeof(uint32_t) * (length - 1U));
  uint32_t *twiddle_shoup = (uint32_t *)malloc(sizeof(uint32_t) * (length - 1U));
  if (!twiddles || !twiddle_shoup) {
    free(twiddles);
    free(twiddle_shoup);
    return 0;
  }

  size_t offset = 0;
  for (size_t span = 2U; span <= length; span <<= 1U) {
    size_t half = span >> 1U;
    uint32_t root = ntt16_mod_pow(XRAY_BIGINT_NTT16_ROOT, (uint32_t)((modulus - 1U) / (uint32_t)span), modulus);
    if (inverse) root = ntt16_mod_pow(root, modulus - 2U, modulus);
    uint64_t factor = 1U;
    for (size_t lane = 0; lane < half; ++lane) {
      twiddles[offset + lane] = (uint32_t)factor;
      twiddle_shoup[offset + lane] = ntt16_shoup_factor((uint32_t)factor, modulus);
      factor = (factor * root) % modulus;
    }
    offset += half;
    if (span == length) break;
  }

  free(cache->twiddles);
  free(cache->twiddle_shoup);
  cache->length = length;
  cache->modulus = modulus;
  cache->inverse = inverse ? 1 : 0;
  cache->twiddles = twiddles;
  cache->twiddle_shoup = twiddle_shoup;
  return 1;
}

static int ntt16_get_twiddle_cache(
  size_t length,
  uint32_t modulus,
  int inverse,
  const uint32_t **twiddles,
  const uint32_t **twiddle_shoup) {
  if (!twiddles || !twiddle_shoup || length < 32768U) return 0;
  int slot = ntt16_twiddle_cache_slot(modulus, inverse);
  if (slot < 0) return 0;
  XrayNtt16TwiddleCache *cache = &ntt16_twiddle_caches[slot];
  if (cache->length != length ||
      cache->modulus != modulus ||
      cache->inverse != (inverse ? 1 : 0) ||
      !cache->twiddles ||
      !cache->twiddle_shoup) {
    return 0;
  }
  *twiddles = cache->twiddles;
  *twiddle_shoup = cache->twiddle_shoup;
  return 1;
}

static int ntt16_transform(uint32_t *values, size_t length, uint32_t modulus, int inverse) {
  if (!values || length == 0 || length > (size_t)XRAY_BIGINT_NTT16_MAX_LENGTH) return 0;
  if ((length & (length - 1U)) != 0) return 0;
  if (((uint32_t)(modulus - 1U) % (uint32_t)length) != 0) return 0;

  const uint32_t *cached_twiddles = NULL;
  const uint32_t *cached_twiddle_shoup = NULL;
  int use_cached_twiddles = ntt16_get_twiddle_cache(
    length,
    modulus,
    inverse,
    &cached_twiddles,
    &cached_twiddle_shoup);
  uint32_t *twiddles = NULL;
  uint32_t *twiddle_shoup = NULL;
  if (!use_cached_twiddles) {
    twiddles = (uint32_t *)malloc(sizeof(uint32_t) * (length >> 1U));
    twiddle_shoup = (uint32_t *)malloc(sizeof(uint32_t) * (length >> 1U));
    if (!twiddles || !twiddle_shoup) {
      free(twiddles);
      free(twiddle_shoup);
      return 0;
    }
  }

  for (size_t index = 1U, reversed = 0; index < length; ++index) {
    size_t bit = length >> 1U;
    while (reversed & bit) {
      reversed ^= bit;
      bit >>= 1U;
    }
    reversed ^= bit;
    if (index < reversed) {
      uint32_t temp = values[index];
      values[index] = values[reversed];
      values[reversed] = temp;
    }
  }

  for (size_t span = 2U; span <= length; span <<= 1U) {
    size_t half = span >> 1U;
    const uint32_t *stage_twiddles = NULL;
    const uint32_t *stage_twiddle_shoup = NULL;
    if (use_cached_twiddles) {
      size_t cache_offset = half - 1U;
      stage_twiddles = cached_twiddles + cache_offset;
      stage_twiddle_shoup = cached_twiddle_shoup + cache_offset;
    } else {
      uint32_t root = ntt16_mod_pow(XRAY_BIGINT_NTT16_ROOT, (uint32_t)((modulus - 1U) / (uint32_t)span), modulus);
      if (inverse) root = ntt16_mod_pow(root, modulus - 2U, modulus);
      uint64_t factor = 1U;
      for (size_t lane = 0; lane < half; ++lane) {
        twiddles[lane] = (uint32_t)factor;
        twiddle_shoup[lane] = ntt16_shoup_factor((uint32_t)factor, modulus);
        factor = (factor * root) % modulus;
      }
      stage_twiddles = twiddles;
      stage_twiddle_shoup = twiddle_shoup;
    }
    for (size_t offset = 0; offset < length; offset += span) {
      for (size_t lane = 0; lane < half; ++lane) {
        uint32_t left = values[offset + lane];
        uint32_t right = ntt16_mul_shoup(
          values[offset + lane + half],
          stage_twiddles[lane],
          stage_twiddle_shoup[lane],
          modulus);
        uint32_t sum = left + right;
        if (sum >= modulus) sum -= modulus;
        values[offset + lane] = sum;
        values[offset + lane + half] = left >= right ? left - right : (uint32_t)(left + modulus - right);
      }
    }
    if (span == length) break;
  }

  if (inverse) {
    uint32_t inverse_length = ntt16_mod_pow((uint32_t)length, modulus - 2U, modulus);
    uint32_t inverse_length_shoup = ntt16_shoup_factor(inverse_length, modulus);
    for (size_t index = 0; index < length; ++index) {
      values[index] = ntt16_mul_shoup(values[index], inverse_length, inverse_length_shoup, modulus);
    }
  }
  free(twiddles);
  free(twiddle_shoup);
  return 1;
}

typedef struct XrayNtt16TransformTask {
  uint32_t *values;
  size_t length;
  uint32_t modulus;
  int inverse;
  int ok;
} XrayNtt16TransformTask;

#if defined(_WIN32) && defined(_MSC_VER)
static unsigned __stdcall run_ntt16_transform_thread(void *context) {
  XrayNtt16TransformTask *task = (XrayNtt16TransformTask *)context;
  if (task) task->ok = ntt16_transform(task->values, task->length, task->modulus, task->inverse);
  return 0;
}

static void __stdcall run_ntt16_transform_pool(
  XrayWinThreadpoolInstance instance,
  void *context,
  XrayWinThreadpoolWork work) {
  (void)instance;
  (void)work;
  XrayNtt16TransformTask *task = (XrayNtt16TransformTask *)context;
  if (task) task->ok = ntt16_transform(task->values, task->length, task->modulus, task->inverse);
}
#endif

static int ntt16_transform_tasks(XrayNtt16TransformTask *tasks, size_t task_count) {
  if (!tasks || task_count == 0) return 0;
  for (size_t index = 0; index < task_count; ++index) {
    if (!ntt16_prepare_twiddle_cache(tasks[index].length, tasks[index].modulus, tasks[index].inverse)) return 0;
  }
#if defined(_WIN32) && defined(_MSC_VER)
  if (task_count > 1U && tasks[0].length >= 32768U) {
    if (tasks[0].length <= 65536U) {
      XrayWinHandle handles[8] = {0};
      size_t started = 0;
      if (task_count > sizeof(handles) / sizeof(handles[0])) return 0;
      for (size_t index = 0; index < task_count; ++index) {
        tasks[index].ok = 0;
        uintptr_t handle = _beginthreadex(NULL, 0, run_ntt16_transform_thread, &tasks[index], 0, NULL);
        if (!handle) break;
        handles[started++] = (XrayWinHandle)handle;
      }
      if (started) {
        WaitForMultipleObjects((unsigned long)started, handles, 1, XRAY_WIN_WAIT_INFINITE);
        for (size_t index = 0; index < started; ++index) CloseHandle(handles[index]);
      }
      if (started == task_count) {
        int ok = 1;
        for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
        return ok;
      }
    }
    XrayWinThreadpoolWork works[8] = {0};
    size_t created = 0;
    if (task_count > sizeof(works) / sizeof(works[0])) return 0;
    for (size_t index = 0; index < task_count; ++index) {
      tasks[index].ok = 0;
      XrayWinThreadpoolWork work = CreateThreadpoolWork(
        run_ntt16_transform_pool,
        &tasks[index],
        NULL);
      if (!work) break;
      works[created++] = work;
    }
    if (created == task_count) {
      for (size_t index = 0; index < created; ++index) SubmitThreadpoolWork(works[index]);
      for (size_t index = 0; index < created; ++index) {
        WaitForThreadpoolWorkCallbacks(works[index], 0);
        CloseThreadpoolWork(works[index]);
      }
      int ok = 1;
      for (size_t index = 0; index < task_count; ++index) ok = ok && tasks[index].ok;
      return ok;
    }
    for (size_t index = 0; index < created; ++index) CloseThreadpoolWork(works[index]);
  }
#endif
  int ok = 1;
  for (size_t index = 0; ok && index < task_count; ++index) {
    tasks[index].ok = ntt16_transform(tasks[index].values, tasks[index].length, tasks[index].modulus, tasks[index].inverse);
    ok = ok && tasks[index].ok;
  }
  return ok;
}

#if defined(_WIN32) && defined(_MSC_VER) && XRAY_BIGINT_HAS_THREAD_LOCAL
typedef struct XrayNtt16TransformWorker {
  XrayWinHandle thread;
  XrayWinHandle start_event;
  XrayWinHandle done_event;
  XrayNtt16TransformTask *task;
  volatile long stop;
} XrayNtt16TransformWorker;

typedef struct XrayNtt16TransformWorkerPool {
  XrayNtt16TransformWorker workers[XRAY_NTT16_TRANSFORM_POOL_WORKERS];
  int initialized;
  int atexit_registered;
} XrayNtt16TransformWorkerPool;

static XRAY_BIGINT_THREAD_LOCAL XrayNtt16TransformWorkerPool ntt16_transform_tls_pool;

static unsigned __stdcall run_ntt16_transform_worker_thread(void *context) {
  XrayNtt16TransformWorker *worker = (XrayNtt16TransformWorker *)context;
  if (!worker) return 0U;
  for (;;) {
    WaitForSingleObject(worker->start_event, XRAY_WIN_WAIT_INFINITE);
    if (worker->stop) {
      SetEvent(worker->done_event);
      return 0U;
    }
    if (worker->task) {
      XrayNtt16TransformTask *task = worker->task;
      task->ok = ntt16_transform(task->values, task->length, task->modulus, task->inverse);
    }
    SetEvent(worker->done_event);
  }
}

static void ntt16_transform_worker_pool_clear(XrayNtt16TransformWorkerPool *pool) {
  if (!pool) return;
  XrayWinHandle threads[XRAY_NTT16_TRANSFORM_POOL_WORKERS];
  size_t thread_count = 0;
  for (size_t index = 0; index < XRAY_NTT16_TRANSFORM_POOL_WORKERS; ++index) {
    XrayNtt16TransformWorker *worker = &pool->workers[index];
    if (worker->thread && worker->start_event && worker->done_event) {
      worker->stop = 1;
      ResetEvent(worker->done_event);
      SetEvent(worker->start_event);
      threads[thread_count++] = worker->thread;
    }
  }
  if (thread_count) WaitForMultipleObjects((unsigned long)thread_count, threads, 1, XRAY_WIN_WAIT_INFINITE);
  for (size_t index = 0; index < XRAY_NTT16_TRANSFORM_POOL_WORKERS; ++index) {
    XrayNtt16TransformWorker *worker = &pool->workers[index];
    if (worker->thread) CloseHandle(worker->thread);
    if (worker->start_event) CloseHandle(worker->start_event);
    if (worker->done_event) CloseHandle(worker->done_event);
    memset(worker, 0, sizeof(*worker));
  }
  pool->initialized = 0;
}

static void ntt16_transform_worker_pool_clear_at_exit(void) {
  ntt16_transform_worker_pool_clear(&ntt16_transform_tls_pool);
}

static int ntt16_transform_worker_pool_prepare(XrayNtt16TransformWorkerPool *pool) {
  if (!pool) return 0;
  if (pool->initialized) return 1;
  memset(pool->workers, 0, sizeof(pool->workers));
  for (size_t index = 0; index < XRAY_NTT16_TRANSFORM_POOL_WORKERS; ++index) {
    XrayNtt16TransformWorker *worker = &pool->workers[index];
    worker->start_event = CreateEventA(NULL, 0, 0, NULL);
    worker->done_event = CreateEventA(NULL, 1, 0, NULL);
    if (!worker->start_event || !worker->done_event) {
      ntt16_transform_worker_pool_clear(pool);
      return 0;
    }
    uintptr_t handle = _beginthreadex(NULL, 0, run_ntt16_transform_worker_thread, worker, 0, NULL);
    if (!handle) {
      ntt16_transform_worker_pool_clear(pool);
      return 0;
    }
    worker->thread = (XrayWinHandle)handle;
  }
  pool->initialized = 1;
  if (!pool->atexit_registered) {
    (void)atexit(ntt16_transform_worker_pool_clear_at_exit);
    pool->atexit_registered = 1;
  }
  return 1;
}

static int ntt16_transform_tasks_persistent(XrayNtt16TransformTask *tasks, size_t task_count) {
  if (!tasks || task_count == 0 || task_count > XRAY_NTT16_TRANSFORM_POOL_WORKERS) return 0;
  if (tasks[0].length < 32768U) return ntt16_transform_tasks(tasks, task_count);
  for (size_t index = 0; index < task_count; ++index) {
    if (!ntt16_prepare_twiddle_cache(tasks[index].length, tasks[index].modulus, tasks[index].inverse)) return 0;
  }
  XrayNtt16TransformWorkerPool *pool = &ntt16_transform_tls_pool;
  if (!ntt16_transform_worker_pool_prepare(pool)) return 0;
  XrayWinHandle done_events[XRAY_NTT16_TRANSFORM_POOL_WORKERS];
  for (size_t index = 0; index < task_count; ++index) {
    XrayNtt16TransformWorker *worker = &pool->workers[index];
    tasks[index].ok = 0;
    worker->task = &tasks[index];
    ResetEvent(worker->done_event);
    done_events[index] = worker->done_event;
    SetEvent(worker->start_event);
  }
  WaitForMultipleObjects((unsigned long)task_count, done_events, 1, XRAY_WIN_WAIT_INFINITE);
  int ok = 1;
  for (size_t index = 0; index < task_count; ++index) {
    pool->workers[index].task = NULL;
    ok = ok && tasks[index].ok;
  }
  return ok;
}
#endif

static int ntt16_transform_tasks_select(
  XrayNtt16TransformTask *tasks,
  size_t task_count,
  int persistent_transforms) {
#if defined(_WIN32) && defined(_MSC_VER) && XRAY_BIGINT_HAS_THREAD_LOCAL
  if (persistent_transforms) return ntt16_transform_tasks_persistent(tasks, task_count);
#else
  (void)persistent_transforms;
#endif
  return ntt16_transform_tasks(tasks, task_count);
}

typedef struct XrayNtt16PointwiseTask {
  uint32_t *left;
  const uint32_t *right;
  size_t length;
  uint32_t modulus;
  int square;
} XrayNtt16PointwiseTask;

static void run_ntt16_pointwise_task(XrayNtt16PointwiseTask *task) {
  if (!task || !task->left) return;
  if (task->square) {
    for (size_t index = 0; index < task->length; ++index) {
      task->left[index] = (uint32_t)(((uint64_t)task->left[index] * task->left[index]) % task->modulus);
    }
  } else {
    for (size_t index = 0; index < task->length; ++index) {
      task->left[index] = (uint32_t)(((uint64_t)task->left[index] * task->right[index]) % task->modulus);
    }
  }
}

#if defined(_WIN32) && defined(_MSC_VER)
static unsigned __stdcall run_ntt16_pointwise_thread(void *context) {
  run_ntt16_pointwise_task((XrayNtt16PointwiseTask *)context);
  return 0;
}
#endif

static int ntt16_pointwise_tasks(XrayNtt16PointwiseTask *tasks, size_t task_count) {
  if (!tasks || task_count == 0) return 0;
#if defined(_WIN32) && defined(_MSC_VER)
  if (task_count > 1U && tasks[0].length >= 32768U) {
    XrayWinHandle handles[8] = {0};
    size_t started = 0;
    if (task_count > sizeof(handles) / sizeof(handles[0])) return 0;
    for (size_t index = 0; index < task_count; ++index) {
      uintptr_t handle = _beginthreadex(NULL, 0, run_ntt16_pointwise_thread, &tasks[index], 0, NULL);
      if (!handle) break;
      handles[started++] = (XrayWinHandle)handle;
    }
    if (started) {
      WaitForMultipleObjects((unsigned long)started, handles, 1, XRAY_WIN_WAIT_INFINITE);
      for (size_t index = 0; index < started; ++index) CloseHandle(handles[index]);
    }
    return started == task_count;
  }
#endif
  for (size_t index = 0; index < task_count; ++index) run_ntt16_pointwise_task(&tasks[index]);
  return 1;
}

static size_t ntt16_digit_count(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 0;
  if (value->count > SIZE_MAX / 4U) return SIZE_MAX;
  size_t digits = value->count * 4U;
  while (digits) {
    size_t digit_index = digits - 1U;
    size_t limb_index = digit_index >> 2U;
    unsigned int shift = (unsigned int)((digit_index & 3U) * XRAY_BIGINT_NTT16_BASE_BITS);
    if (((value->limbs[limb_index] >> shift) & XRAY_BIGINT_NTT16_BASE_MASK) != 0) break;
    digits--;
  }
  return digits;
}

static int ntt16_coefficient_bound_supported(size_t left_digits, size_t right_digits) {
  size_t min_digits = left_digits < right_digits ? left_digits : right_digits;
  uint64_t max_digit_product = XRAY_BIGINT_NTT16_BASE_MASK * XRAY_BIGINT_NTT16_BASE_MASK;
  uint64_t crt_modulus = (uint64_t)XRAY_BIGINT_NTT16_MOD0 * (uint64_t)XRAY_BIGINT_NTT16_MOD1;
  return min_digits <= (size_t)((crt_modulus - 1U) / max_digit_product);
}

static int ntt16_next_length(size_t required, size_t *out_length) {
  if (!out_length || required == 0) return 0;
  size_t length = 1U;
  while (length < required) {
    if (length > (size_t)XRAY_BIGINT_NTT16_MAX_LENGTH / 2U) return 0;
    length <<= 1U;
  }
  if (length > (size_t)XRAY_BIGINT_NTT16_MAX_LENGTH) return 0;
  *out_length = length;
  return 1;
}

static void ntt16_fill_digits(uint32_t *digits, const XrayScratchBigInt *value, size_t digit_count) {
  for (size_t index = 0; index < digit_count; ++index) {
    size_t limb_index = index >> 2U;
    unsigned int shift = (unsigned int)((index & 3U) * XRAY_BIGINT_NTT16_BASE_BITS);
    digits[index] = (uint32_t)((value->limbs[limb_index] >> shift) & XRAY_BIGINT_NTT16_BASE_MASK);
  }
}

static uint64_t ntt16_crt_coefficient(uint32_t residue0, uint32_t residue1, uint32_t inverse_mod0_mod1) {
  uint64_t left = residue0;
  uint64_t right = residue1;
  uint64_t delta = right >= left ? right - left : right + XRAY_BIGINT_NTT16_MOD1 - left;
  uint64_t scale = (delta * inverse_mod0_mod1) % XRAY_BIGINT_NTT16_MOD1;
  return left + (uint64_t)XRAY_BIGINT_NTT16_MOD0 * scale;
}

static int ntt16_store_digit(XrayScratchBigInt *out, size_t digit_index, uint32_t digit, size_t digit_capacity) {
  if (!out || digit_index >= digit_capacity) return 0;
  size_t limb_index = digit_index >> 2U;
  unsigned int shift = (unsigned int)((digit_index & 3U) * XRAY_BIGINT_NTT16_BASE_BITS);
  out->limbs[limb_index] |= (uint64_t)digit << shift;
  return 1;
}

static int ntt16_reconstruct_to_bigint(
  XrayScratchBigInt *out,
  const uint32_t *residue0,
  const uint32_t *residue1,
  size_t coefficient_count,
  size_t left_digits,
  size_t right_digits) {
  if (!out || !residue0 || !residue1 || coefficient_count == 0) return 0;
  if (left_digits > SIZE_MAX - right_digits) return 0;
  size_t digit_capacity = left_digits + right_digits;
  if (digit_capacity > SIZE_MAX - 4U) return 0;
  digit_capacity += 4U;
  size_t limb_capacity = (digit_capacity + 3U) >> 2U;
  if (!reserve_limbs(out, limb_capacity)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * limb_capacity);

  uint32_t inverse_mod0_mod1 = ntt16_mod_pow(XRAY_BIGINT_NTT16_MOD0, XRAY_BIGINT_NTT16_MOD1 - 2U, XRAY_BIGINT_NTT16_MOD1);
  uint64_t carry = 0;
  size_t digit_index = 0;
  for (size_t coeff_index = 0; coeff_index < coefficient_count; ++coeff_index) {
    uint64_t coefficient = ntt16_crt_coefficient(residue0[coeff_index], residue1[coeff_index], inverse_mod0_mod1);
    if (carry > UINT64_MAX - coefficient) return 0;
    uint64_t value = coefficient + carry;
    if (!ntt16_store_digit(out, digit_index++, (uint32_t)(value & XRAY_BIGINT_NTT16_BASE_MASK), digit_capacity)) return 0;
    carry = value >> XRAY_BIGINT_NTT16_BASE_BITS;
  }
  while (carry) {
    if (!ntt16_store_digit(out, digit_index++, (uint32_t)(carry & XRAY_BIGINT_NTT16_BASE_MASK), digit_capacity)) return 0;
    carry >>= XRAY_BIGINT_NTT16_BASE_BITS;
  }

  out->count = (digit_index + 3U) >> 2U;
  normalize(out);
  return 1;
}

typedef struct XrayNtt32Uint128 {
  uint64_t low;
  uint64_t high;
} XrayNtt32Uint128;

static XrayNtt32Uint128 ntt32_u128_from_u64(uint64_t value) {
  XrayNtt32Uint128 result;
  result.low = value;
  result.high = 0;
  return result;
}

static XrayNtt32Uint128 ntt32_mul_u64_u32(uint64_t left, uint32_t right) {
  XrayNtt32Uint128 result;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(left, right, &high);
  result.low = (uint64_t)low;
  result.high = (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)left * (__uint128_t)right;
  result.low = (uint64_t)product;
  result.high = (uint64_t)(product >> 64);
#endif
  return result;
}

static void ntt32_u128_add_u64(XrayNtt32Uint128 *value, uint64_t addend) {
  uint64_t before = value->low;
  value->low += addend;
  if (value->low < before) value->high++;
}

static void ntt32_u128_add(XrayNtt32Uint128 *value, XrayNtt32Uint128 addend) {
  uint64_t before = value->low;
  value->low += addend.low;
  value->high += addend.high + (value->low < before ? 1U : 0U);
}

static size_t ntt32_digit_count(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 0;
  if (value->count > SIZE_MAX / 2U) return SIZE_MAX;
  size_t digits = value->count * 2U;
  while (digits) {
    size_t digit_index = digits - 1U;
    size_t limb_index = digit_index >> 1U;
    unsigned int shift = (unsigned int)((digit_index & 1U) * 32U);
    if ((uint32_t)(value->limbs[limb_index] >> shift) != 0) break;
    digits--;
  }
  return digits;
}

static int ntt32_coefficient_bound_supported(size_t left_digits, size_t right_digits) {
  size_t min_digits = left_digits < right_digits ? left_digits : right_digits;
  return min_digits <= XRAY_BIGINT_NTT32_MAX_SAFE_DIGITS;
}

static void ntt32_fill_digits_all_mods(
  uint32_t *digits0,
  uint32_t *digits1,
  uint32_t *digits2,
  const XrayScratchBigInt *value,
  size_t digit_count) {
  for (size_t index = 0; index < digit_count; ++index) {
    size_t limb_index = index >> 1U;
    unsigned int shift = (unsigned int)((index & 1U) * 32U);
    uint32_t digit = (uint32_t)(value->limbs[limb_index] >> shift);
    digits0[index] = (uint32_t)(digit % XRAY_BIGINT_NTT16_MOD0);
    digits1[index] = (uint32_t)(digit % XRAY_BIGINT_NTT16_MOD1);
    digits2[index] = (uint32_t)(digit % XRAY_BIGINT_NTT32_MOD2);
  }
}

static int ntt32_store_digit(XrayScratchBigInt *out, size_t digit_index, uint32_t digit, size_t digit_capacity) {
  if (!out || digit_index >= digit_capacity) return 0;
  size_t limb_index = digit_index >> 1U;
  unsigned int shift = (unsigned int)((digit_index & 1U) * 32U);
  out->limbs[limb_index] |= (uint64_t)digit << shift;
  return 1;
}

static uint32_t ntt32_reduce_mod2_u32(uint32_t value) {
  if (value >= XRAY_BIGINT_NTT32_MOD2) value -= XRAY_BIGINT_NTT32_MOD2;
  if (value >= XRAY_BIGINT_NTT32_MOD2) value -= XRAY_BIGINT_NTT32_MOD2;
  return value;
}

static XrayNtt32Uint128 ntt32_crt_coefficient(
  uint32_t residue0,
  uint32_t residue1,
  uint32_t residue2,
  uint32_t inverse_mod0_mod1,
  uint32_t inverse_mod0_mod1_shoup,
  uint32_t inverse_mod0mod1_mod2,
  uint32_t inverse_mod0mod1_mod2_shoup,
  uint32_t mod0_mod2,
  uint32_t mod0_mod2_shoup,
  uint64_t mod0_mod1) {
  uint64_t delta1 = residue1 >= residue0 ?
    (uint64_t)residue1 - residue0 :
    (uint64_t)residue1 + XRAY_BIGINT_NTT16_MOD1 - residue0;
  uint32_t t1 = ntt16_mul_shoup(
    (uint32_t)delta1,
    inverse_mod0_mod1,
    inverse_mod0_mod1_shoup,
    XRAY_BIGINT_NTT16_MOD1);
  uint32_t mod2_product = ntt16_mul_shoup(
    ntt32_reduce_mod2_u32(t1),
    mod0_mod2,
    mod0_mod2_shoup,
    XRAY_BIGINT_NTT32_MOD2);
  uint32_t x_mod2 = ntt32_reduce_mod2_u32(residue0);
  x_mod2 += mod2_product;
  if (x_mod2 >= XRAY_BIGINT_NTT32_MOD2) x_mod2 -= XRAY_BIGINT_NTT32_MOD2;
  uint64_t delta2 = residue2 >= x_mod2 ?
    (uint64_t)residue2 - x_mod2 :
    (uint64_t)residue2 + XRAY_BIGINT_NTT32_MOD2 - x_mod2;
  uint32_t t2 = ntt16_mul_shoup(
    (uint32_t)delta2,
    inverse_mod0mod1_mod2,
    inverse_mod0mod1_mod2_shoup,
    XRAY_BIGINT_NTT32_MOD2);

  XrayNtt32Uint128 coefficient = ntt32_u128_from_u64(residue0);
  ntt32_u128_add_u64(&coefficient, (uint64_t)XRAY_BIGINT_NTT16_MOD0 * t1);
  ntt32_u128_add(&coefficient, ntt32_mul_u64_u32(mod0_mod1, t2));
  return coefficient;
}

typedef struct XrayNtt32CrtTask {
  const uint32_t *residue0;
  const uint32_t *residue1;
  const uint32_t *residue2;
  uint64_t *coefficient_low;
  uint64_t *coefficient_high;
  size_t start;
  size_t end;
  uint32_t inverse_mod0_mod1;
  uint32_t inverse_mod0_mod1_shoup;
  uint32_t inverse_mod0mod1_mod2;
  uint32_t inverse_mod0mod1_mod2_shoup;
  uint32_t mod0_mod2;
  uint32_t mod0_mod2_shoup;
  uint64_t mod0_mod1;
} XrayNtt32CrtTask;

static void run_ntt32_crt_task(XrayNtt32CrtTask *task) {
  if (!task) return;
  for (size_t index = task->start; index < task->end; ++index) {
    XrayNtt32Uint128 coefficient = ntt32_crt_coefficient(
      task->residue0[index],
      task->residue1[index],
      task->residue2[index],
      task->inverse_mod0_mod1,
      task->inverse_mod0_mod1_shoup,
      task->inverse_mod0mod1_mod2,
      task->inverse_mod0mod1_mod2_shoup,
      task->mod0_mod2,
      task->mod0_mod2_shoup,
      task->mod0_mod1);
    task->coefficient_low[index] = coefficient.low;
    task->coefficient_high[index] = coefficient.high;
  }
}

#if defined(_WIN32) && defined(_MSC_VER)
static void __stdcall run_ntt32_crt_pool(
  XrayWinThreadpoolInstance instance,
  void *context,
  XrayWinThreadpoolWork work) {
  (void)instance;
  (void)work;
  run_ntt32_crt_task((XrayNtt32CrtTask *)context);
}
#endif

static int ntt32_compute_crt_coefficients(
  const uint32_t *residue0,
  const uint32_t *residue1,
  const uint32_t *residue2,
  uint64_t *coefficient_low,
  uint64_t *coefficient_high,
  size_t coefficient_count,
  uint32_t inverse_mod0_mod1,
  uint32_t inverse_mod0_mod1_shoup,
  uint32_t inverse_mod0mod1_mod2,
  uint32_t inverse_mod0mod1_mod2_shoup,
  uint32_t mod0_mod2,
  uint32_t mod0_mod2_shoup,
  uint64_t mod0_mod1) {
  if (!residue0 || !residue1 || !residue2 || !coefficient_low || !coefficient_high) return 0;
  size_t task_count = coefficient_count >= 32768U ? 4U : 1U;
  XrayNtt32CrtTask tasks[4];
  memset(tasks, 0, sizeof(tasks));
  for (size_t task_index = 0; task_index < task_count; ++task_index) {
    size_t start = (coefficient_count * task_index) / task_count;
    size_t end = (coefficient_count * (task_index + 1U)) / task_count;
    tasks[task_index].residue0 = residue0;
    tasks[task_index].residue1 = residue1;
    tasks[task_index].residue2 = residue2;
    tasks[task_index].coefficient_low = coefficient_low;
    tasks[task_index].coefficient_high = coefficient_high;
    tasks[task_index].start = start;
    tasks[task_index].end = end;
    tasks[task_index].inverse_mod0_mod1 = inverse_mod0_mod1;
    tasks[task_index].inverse_mod0_mod1_shoup = inverse_mod0_mod1_shoup;
    tasks[task_index].inverse_mod0mod1_mod2 = inverse_mod0mod1_mod2;
    tasks[task_index].inverse_mod0mod1_mod2_shoup = inverse_mod0mod1_mod2_shoup;
    tasks[task_index].mod0_mod2 = mod0_mod2;
    tasks[task_index].mod0_mod2_shoup = mod0_mod2_shoup;
    tasks[task_index].mod0_mod1 = mod0_mod1;
  }
#if defined(_WIN32) && defined(_MSC_VER)
  if (task_count > 1U) {
    XrayWinThreadpoolWork works[4] = {0};
    size_t created = 0;
    for (size_t index = 0; index < task_count; ++index) {
      XrayWinThreadpoolWork work = CreateThreadpoolWork(
        run_ntt32_crt_pool,
        &tasks[index],
        NULL);
      if (!work) break;
      works[created++] = work;
    }
    if (created == task_count) {
      for (size_t index = 0; index < created; ++index) SubmitThreadpoolWork(works[index]);
      for (size_t index = 0; index < created; ++index) {
        WaitForThreadpoolWorkCallbacks(works[index], 0);
        CloseThreadpoolWork(works[index]);
      }
      return 1;
    }
    for (size_t index = 0; index < created; ++index) CloseThreadpoolWork(works[index]);
  }
#endif
  for (size_t index = 0; index < task_count; ++index) run_ntt32_crt_task(&tasks[index]);
  return 1;
}

static int ntt32_reconstruct_to_bigint(
  XrayScratchBigInt *out,
  const uint32_t *residue0,
  const uint32_t *residue1,
  const uint32_t *residue2,
  size_t coefficient_count,
  size_t left_digits,
  size_t right_digits,
  uint64_t *coefficient_low_buffer,
  uint64_t *coefficient_high_buffer,
  size_t coefficient_buffer_capacity) {
  if (!out || !residue0 || !residue1 || !residue2 || coefficient_count == 0) return 0;
  if (left_digits > SIZE_MAX - right_digits) return 0;
  size_t digit_capacity = left_digits + right_digits;
  if (digit_capacity > SIZE_MAX - 2U) return 0;
  digit_capacity += 2U;
  size_t limb_capacity = (digit_capacity + 1U) >> 1U;
  if (!reserve_limbs(out, limb_capacity)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * limb_capacity);

  uint32_t inverse_mod0_mod1 = ntt16_mod_pow(XRAY_BIGINT_NTT16_MOD0, XRAY_BIGINT_NTT16_MOD1 - 2U, XRAY_BIGINT_NTT16_MOD1);
  uint32_t inverse_mod0_mod1_shoup = ntt16_shoup_factor(inverse_mod0_mod1, XRAY_BIGINT_NTT16_MOD1);
  uint32_t mod0_mod2 = XRAY_BIGINT_NTT16_MOD0 % XRAY_BIGINT_NTT32_MOD2;
  uint32_t mod0_mod2_shoup = ntt16_shoup_factor(mod0_mod2, XRAY_BIGINT_NTT32_MOD2);
  uint32_t mod1_mod2 = XRAY_BIGINT_NTT16_MOD1 % XRAY_BIGINT_NTT32_MOD2;
  uint32_t mod0mod1_mod2 = (uint32_t)(((uint64_t)mod0_mod2 * mod1_mod2) % XRAY_BIGINT_NTT32_MOD2);
  uint32_t inverse_mod0mod1_mod2 = ntt16_mod_pow(mod0mod1_mod2, XRAY_BIGINT_NTT32_MOD2 - 2U, XRAY_BIGINT_NTT32_MOD2);
  uint32_t inverse_mod0mod1_mod2_shoup = ntt16_shoup_factor(inverse_mod0mod1_mod2, XRAY_BIGINT_NTT32_MOD2);
  uint64_t mod0_mod1 = (uint64_t)XRAY_BIGINT_NTT16_MOD0 * XRAY_BIGINT_NTT16_MOD1;

  int owns_coefficient_buffers = 0;
  uint64_t *coefficient_low = coefficient_low_buffer;
  uint64_t *coefficient_high = coefficient_high_buffer;
  if (!coefficient_low || !coefficient_high || coefficient_buffer_capacity < coefficient_count) {
    coefficient_low = (uint64_t *)malloc(sizeof(uint64_t) * coefficient_count);
    coefficient_high = (uint64_t *)malloc(sizeof(uint64_t) * coefficient_count);
    owns_coefficient_buffers = 1;
  }
  if (!coefficient_low || !coefficient_high) {
    if (owns_coefficient_buffers) {
      free(coefficient_low);
      free(coefficient_high);
    }
    return 0;
  }
  int ok = ntt32_compute_crt_coefficients(
    residue0,
    residue1,
    residue2,
    coefficient_low,
    coefficient_high,
    coefficient_count,
    inverse_mod0_mod1,
    inverse_mod0_mod1_shoup,
    inverse_mod0mod1_mod2,
    inverse_mod0mod1_mod2_shoup,
    mod0_mod2,
    mod0_mod2_shoup,
    mod0_mod1);

  uint64_t carry = 0;
  size_t digit_index = 0;
  for (size_t coeff_index = 0; ok && coeff_index < coefficient_count; ++coeff_index) {
    XrayNtt32Uint128 value;
    value.low = coefficient_low[coeff_index];
    value.high = coefficient_high[coeff_index];
    ntt32_u128_add_u64(&value, carry);
    if (value.high >> 32U) {
      ok = 0;
      break;
    }
    if (!ntt32_store_digit(out, digit_index++, (uint32_t)value.low, digit_capacity)) {
      ok = 0;
      break;
    }
    carry = (value.low >> 32U) | (value.high << 32U);
  }
  while (ok && carry) {
    if (!ntt32_store_digit(out, digit_index++, (uint32_t)carry, digit_capacity)) {
      ok = 0;
      break;
    }
    carry >>= 32U;
  }

  if (owns_coefficient_buffers) {
    free(coefficient_low);
    free(coefficient_high);
  }
  if (!ok) return 0;
  out->count = (digit_index + 1U) >> 1U;
  normalize(out);
  return 1;
}

static int ntt32_reconstruct_to_bigint_direct(
  XrayScratchBigInt *out,
  const uint32_t *residue0,
  const uint32_t *residue1,
  const uint32_t *residue2,
  size_t coefficient_count,
  size_t left_digits,
  size_t right_digits) {
  if (!out || !residue0 || !residue1 || !residue2 || coefficient_count == 0) return 0;
  if (left_digits > SIZE_MAX - right_digits) return 0;
  size_t digit_capacity = left_digits + right_digits;
  if (digit_capacity > SIZE_MAX - 2U) return 0;
  digit_capacity += 2U;
  size_t limb_capacity = (digit_capacity + 1U) >> 1U;
  if (!reserve_limbs(out, limb_capacity)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * limb_capacity);

  uint32_t inverse_mod0_mod1 = ntt16_mod_pow(XRAY_BIGINT_NTT16_MOD0, XRAY_BIGINT_NTT16_MOD1 - 2U, XRAY_BIGINT_NTT16_MOD1);
  uint32_t inverse_mod0_mod1_shoup = ntt16_shoup_factor(inverse_mod0_mod1, XRAY_BIGINT_NTT16_MOD1);
  uint32_t mod0_mod2 = XRAY_BIGINT_NTT16_MOD0 % XRAY_BIGINT_NTT32_MOD2;
  uint32_t mod0_mod2_shoup = ntt16_shoup_factor(mod0_mod2, XRAY_BIGINT_NTT32_MOD2);
  uint32_t mod1_mod2 = XRAY_BIGINT_NTT16_MOD1 % XRAY_BIGINT_NTT32_MOD2;
  uint32_t mod0mod1_mod2 = (uint32_t)(((uint64_t)mod0_mod2 * mod1_mod2) % XRAY_BIGINT_NTT32_MOD2);
  uint32_t inverse_mod0mod1_mod2 = ntt16_mod_pow(mod0mod1_mod2, XRAY_BIGINT_NTT32_MOD2 - 2U, XRAY_BIGINT_NTT32_MOD2);
  uint32_t inverse_mod0mod1_mod2_shoup = ntt16_shoup_factor(inverse_mod0mod1_mod2, XRAY_BIGINT_NTT32_MOD2);
  uint64_t mod0_mod1 = (uint64_t)XRAY_BIGINT_NTT16_MOD0 * XRAY_BIGINT_NTT16_MOD1;

  uint64_t carry = 0;
  size_t digit_index = 0;
  for (size_t coeff_index = 0; coeff_index < coefficient_count; ++coeff_index) {
    XrayNtt32Uint128 value = ntt32_crt_coefficient(
      residue0[coeff_index],
      residue1[coeff_index],
      residue2[coeff_index],
      inverse_mod0_mod1,
      inverse_mod0_mod1_shoup,
      inverse_mod0mod1_mod2,
      inverse_mod0mod1_mod2_shoup,
      mod0_mod2,
      mod0_mod2_shoup,
      mod0_mod1);
    ntt32_u128_add_u64(&value, carry);
    if (value.high >> 32U) return 0;
    if (!ntt32_store_digit(out, digit_index++, (uint32_t)value.low, digit_capacity)) return 0;
    carry = (value.low >> 32U) | (value.high << 32U);
  }
  while (carry) {
    if (!ntt32_store_digit(out, digit_index++, (uint32_t)carry, digit_capacity)) return 0;
    carry >>= 32U;
  }

  out->count = (digit_index + 1U) >> 1U;
  normalize(out);
  return 1;
}

typedef struct XrayNtt32SquareWorkspace {
  uint32_t *value0;
  uint32_t *value1;
  uint32_t *value2;
  size_t transform_capacity;
  uint64_t *coefficient_low;
  uint64_t *coefficient_high;
  size_t coefficient_capacity;
  int atexit_registered;
} XrayNtt32SquareWorkspace;

#if XRAY_BIGINT_HAS_THREAD_LOCAL
static XRAY_BIGINT_THREAD_LOCAL XrayNtt32SquareWorkspace ntt32_square_tls_workspace;

static void ntt32_square_workspace_clear(XrayNtt32SquareWorkspace *workspace) {
  if (!workspace) return;
  free(workspace->value0);
  free(workspace->value1);
  free(workspace->value2);
  free(workspace->coefficient_low);
  free(workspace->coefficient_high);
  workspace->value0 = NULL;
  workspace->value1 = NULL;
  workspace->value2 = NULL;
  workspace->transform_capacity = 0;
  workspace->coefficient_low = NULL;
  workspace->coefficient_high = NULL;
  workspace->coefficient_capacity = 0;
}

static void ntt32_square_tls_workspace_clear_at_exit(void) {
  ntt32_square_workspace_clear(&ntt32_square_tls_workspace);
}

static int ntt32_square_workspace_prepare(
  XrayNtt32SquareWorkspace *workspace,
  size_t transform_length,
  size_t coefficient_count) {
  if (!workspace || transform_length == 0 || coefficient_count == 0) return 0;
  if (!workspace->atexit_registered) {
    (void)atexit(ntt32_square_tls_workspace_clear_at_exit);
    workspace->atexit_registered = 1;
  }
  if (workspace->transform_capacity < transform_length) {
    uint32_t *value0 = (uint32_t *)malloc(sizeof(uint32_t) * transform_length);
    uint32_t *value1 = (uint32_t *)malloc(sizeof(uint32_t) * transform_length);
    uint32_t *value2 = (uint32_t *)malloc(sizeof(uint32_t) * transform_length);
    if (!value0 || !value1 || !value2) {
      free(value0);
      free(value1);
      free(value2);
      return 0;
    }
    free(workspace->value0);
    free(workspace->value1);
    free(workspace->value2);
    workspace->value0 = value0;
    workspace->value1 = value1;
    workspace->value2 = value2;
    workspace->transform_capacity = transform_length;
  }
  if (workspace->coefficient_capacity < coefficient_count) {
    uint64_t *coefficient_low = (uint64_t *)malloc(sizeof(uint64_t) * coefficient_count);
    uint64_t *coefficient_high = (uint64_t *)malloc(sizeof(uint64_t) * coefficient_count);
    if (!coefficient_low || !coefficient_high) {
      free(coefficient_low);
      free(coefficient_high);
      return 0;
    }
    free(workspace->coefficient_low);
    free(workspace->coefficient_high);
    workspace->coefficient_low = coefficient_low;
    workspace->coefficient_high = coefficient_high;
    workspace->coefficient_capacity = coefficient_count;
  }
  return 1;
}
#endif

static int mul_ntt16_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);

  size_t left_digits = ntt16_digit_count(left);
  size_t right_digits = ntt16_digit_count(right);
  if (left_digits == SIZE_MAX || right_digits == SIZE_MAX) return 0;
  if (left_digits == 0 || right_digits == 0) return set_u32(out, 0);
  if (left_digits > SIZE_MAX - right_digits) return 0;
  size_t coefficient_count = left_digits + right_digits - 1U;
  if (!ntt16_coefficient_bound_supported(left_digits, right_digits)) return 0;

  size_t transform_length = 0;
  if (!ntt16_next_length(coefficient_count, &transform_length)) return 0;

  uint32_t *left0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *right0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *left1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *right1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  if (!left0 || !right0 || !left1 || !right1) {
    free(left0);
    free(right0);
    free(left1);
    free(right1);
    return 0;
  }

  ntt16_fill_digits(left0, left, left_digits);
  ntt16_fill_digits(right0, right, right_digits);
  memcpy(left1, left0, sizeof(uint32_t) * left_digits);
  memcpy(right1, right0, sizeof(uint32_t) * right_digits);

  XrayNtt16TransformTask forward_tasks[] = {
    {left0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {right0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {left1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0},
    {right1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0}
  };
  int ok = ntt16_transform_tasks(forward_tasks, sizeof(forward_tasks) / sizeof(forward_tasks[0]));
  if (ok) {
    for (size_t index = 0; index < transform_length; ++index) {
      left0[index] = (uint32_t)(((uint64_t)left0[index] * right0[index]) % XRAY_BIGINT_NTT16_MOD0);
      left1[index] = (uint32_t)(((uint64_t)left1[index] * right1[index]) % XRAY_BIGINT_NTT16_MOD1);
    }
    XrayNtt16TransformTask inverse_tasks[] = {
      {left0, transform_length, XRAY_BIGINT_NTT16_MOD0, 1, 0},
      {left1, transform_length, XRAY_BIGINT_NTT16_MOD1, 1, 0}
    };
    ok =
      ntt16_transform_tasks(inverse_tasks, sizeof(inverse_tasks) / sizeof(inverse_tasks[0])) &&
      ntt16_reconstruct_to_bigint(out, left0, left1, coefficient_count, left_digits, right_digits);
  }

  free(left0);
  free(right0);
  free(left1);
  free(right1);
  return ok;
}

int xray_bigint_mul_ntt16_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_ntt16_probe_internal(&temp, left, right);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_ntt16_probe_internal(out, left, right);
}

static int square_ntt16_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);

  size_t digit_count = ntt16_digit_count(value);
  if (digit_count == SIZE_MAX) return 0;
  if (digit_count == 0) return set_u32(out, 0);
  if (digit_count > SIZE_MAX / 2U) return 0;
  size_t coefficient_count = digit_count * 2U - 1U;
  if (!ntt16_coefficient_bound_supported(digit_count, digit_count)) return 0;

  size_t transform_length = 0;
  if (!ntt16_next_length(coefficient_count, &transform_length)) return 0;

  uint32_t *value0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *value1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  if (!value0 || !value1) {
    free(value0);
    free(value1);
    return 0;
  }

  ntt16_fill_digits(value0, value, digit_count);
  memcpy(value1, value0, sizeof(uint32_t) * digit_count);

  XrayNtt16TransformTask forward_tasks[] = {
    {value0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {value1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0}
  };
  int ok = ntt16_transform_tasks(forward_tasks, sizeof(forward_tasks) / sizeof(forward_tasks[0]));
  if (ok) {
    for (size_t index = 0; index < transform_length; ++index) {
      value0[index] = (uint32_t)(((uint64_t)value0[index] * value0[index]) % XRAY_BIGINT_NTT16_MOD0);
      value1[index] = (uint32_t)(((uint64_t)value1[index] * value1[index]) % XRAY_BIGINT_NTT16_MOD1);
    }
    XrayNtt16TransformTask inverse_tasks[] = {
      {value0, transform_length, XRAY_BIGINT_NTT16_MOD0, 1, 0},
      {value1, transform_length, XRAY_BIGINT_NTT16_MOD1, 1, 0}
    };
    ok =
      ntt16_transform_tasks(inverse_tasks, sizeof(inverse_tasks) / sizeof(inverse_tasks[0])) &&
      ntt16_reconstruct_to_bigint(out, value0, value1, coefficient_count, digit_count, digit_count);
  }

  free(value0);
  free(value1);
  return ok;
}

int xray_bigint_square_ntt16_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt16_probe_internal(&temp, value);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt16_probe_internal(out, value);
}

static int mul_ntt32_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);

  size_t left_digits = ntt32_digit_count(left);
  size_t right_digits = ntt32_digit_count(right);
  if (left_digits == SIZE_MAX || right_digits == SIZE_MAX) return 0;
  if (left_digits == 0 || right_digits == 0) return set_u32(out, 0);
  if (left_digits > SIZE_MAX - right_digits) return 0;
  size_t coefficient_count = left_digits + right_digits - 1U;
  if (!ntt32_coefficient_bound_supported(left_digits, right_digits)) return 0;

  size_t transform_length = 0;
  if (!ntt16_next_length(coefficient_count, &transform_length)) return 0;

  uint32_t *left0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *right0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *left1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *right1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *left2 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  uint32_t *right2 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  if (!left0 || !right0 || !left1 || !right1 || !left2 || !right2) {
    free(left0);
    free(right0);
    free(left1);
    free(right1);
    free(left2);
    free(right2);
    return 0;
  }

  ntt32_fill_digits_all_mods(left0, left1, left2, left, left_digits);
  ntt32_fill_digits_all_mods(right0, right1, right2, right, right_digits);

  XrayNtt16TransformTask forward_tasks[] = {
    {left0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {right0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {left1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0},
    {right1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0},
    {left2, transform_length, XRAY_BIGINT_NTT32_MOD2, 0, 0},
    {right2, transform_length, XRAY_BIGINT_NTT32_MOD2, 0, 0}
  };
  int ok = ntt16_transform_tasks(forward_tasks, sizeof(forward_tasks) / sizeof(forward_tasks[0]));
  if (ok) {
    for (size_t index = 0; index < transform_length; ++index) {
      left0[index] = (uint32_t)(((uint64_t)left0[index] * right0[index]) % XRAY_BIGINT_NTT16_MOD0);
      left1[index] = (uint32_t)(((uint64_t)left1[index] * right1[index]) % XRAY_BIGINT_NTT16_MOD1);
      left2[index] = (uint32_t)(((uint64_t)left2[index] * right2[index]) % XRAY_BIGINT_NTT32_MOD2);
    }
    XrayNtt16TransformTask inverse_tasks[] = {
      {left0, transform_length, XRAY_BIGINT_NTT16_MOD0, 1, 0},
      {left1, transform_length, XRAY_BIGINT_NTT16_MOD1, 1, 0},
      {left2, transform_length, XRAY_BIGINT_NTT32_MOD2, 1, 0}
    };
    ok =
      ntt16_transform_tasks(inverse_tasks, sizeof(inverse_tasks) / sizeof(inverse_tasks[0])) &&
      ntt32_reconstruct_to_bigint(out, left0, left1, left2, coefficient_count, left_digits, right_digits, NULL, NULL, 0);
  }

  free(left0);
  free(right0);
  free(left1);
  free(right1);
  free(left2);
  free(right2);
  return ok;
}

int xray_bigint_mul_ntt32_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_ntt32_probe_internal(&temp, left, right);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_ntt32_probe_internal(out, left, right);
}

static int square_ntt32_probe_internal_mode(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  int reuse_buffers,
  int parallel_pointwise,
  int persistent_transforms,
  int tailmap_policy,
  int direct_crt) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);

  size_t digit_count = ntt32_digit_count(value);
  if (digit_count == SIZE_MAX) return 0;
  if (digit_count == 0) return set_u32(out, 0);
  if (digit_count > SIZE_MAX / 2U) return 0;
  size_t coefficient_count = digit_count * 2U - 1U;
  if (!ntt32_coefficient_bound_supported(digit_count, digit_count)) return 0;

  size_t transform_length = 0;
  if (!ntt16_next_length(coefficient_count, &transform_length)) return 0;
  if (tailmap_policy) {
    parallel_pointwise = 0;
    if (transform_length <= 65536U) {
      reuse_buffers = 0;
      persistent_transforms = 1;
    } else if (transform_length <= 131072U) {
      reuse_buffers = 1;
      persistent_transforms = 1;
    } else {
      reuse_buffers = tailmap_policy == 1;
      persistent_transforms = 0;
    }
  }

  int using_workspace = 0;
  uint32_t *value0 = NULL;
  uint32_t *value1 = NULL;
  uint32_t *value2 = NULL;
  uint64_t *coefficient_low = NULL;
  uint64_t *coefficient_high = NULL;
  size_t coefficient_capacity = 0;
#if XRAY_BIGINT_HAS_THREAD_LOCAL
  if (reuse_buffers) {
    XrayNtt32SquareWorkspace *workspace = &ntt32_square_tls_workspace;
    if (!ntt32_square_workspace_prepare(workspace, transform_length, coefficient_count)) return 0;
    using_workspace = 1;
    value0 = workspace->value0;
    value1 = workspace->value1;
    value2 = workspace->value2;
    coefficient_low = workspace->coefficient_low;
    coefficient_high = workspace->coefficient_high;
    coefficient_capacity = workspace->coefficient_capacity;
    memset(value0, 0, sizeof(uint32_t) * transform_length);
    memset(value1, 0, sizeof(uint32_t) * transform_length);
    memset(value2, 0, sizeof(uint32_t) * transform_length);
  }
#else
  (void)reuse_buffers;
#endif
  if (!using_workspace) {
    value0 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
    value1 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
    value2 = (uint32_t *)calloc(transform_length, sizeof(uint32_t));
  }
  if (!value0 || !value1 || !value2) {
    if (!using_workspace) {
      free(value0);
      free(value1);
      free(value2);
    }
    return 0;
  }

  ntt32_fill_digits_all_mods(value0, value1, value2, value, digit_count);

  XrayNtt16TransformTask forward_tasks[] = {
    {value0, transform_length, XRAY_BIGINT_NTT16_MOD0, 0, 0},
    {value1, transform_length, XRAY_BIGINT_NTT16_MOD1, 0, 0},
    {value2, transform_length, XRAY_BIGINT_NTT32_MOD2, 0, 0}
  };
  int ok = ntt16_transform_tasks_select(forward_tasks, sizeof(forward_tasks) / sizeof(forward_tasks[0]), persistent_transforms);
  if (ok) {
    if (parallel_pointwise) {
      XrayNtt16PointwiseTask pointwise_tasks[] = {
        {value0, NULL, transform_length, XRAY_BIGINT_NTT16_MOD0, 1},
        {value1, NULL, transform_length, XRAY_BIGINT_NTT16_MOD1, 1},
        {value2, NULL, transform_length, XRAY_BIGINT_NTT32_MOD2, 1}
      };
      ok = ntt16_pointwise_tasks(pointwise_tasks, sizeof(pointwise_tasks) / sizeof(pointwise_tasks[0]));
    } else {
      for (size_t index = 0; index < transform_length; ++index) {
        value0[index] = (uint32_t)(((uint64_t)value0[index] * value0[index]) % XRAY_BIGINT_NTT16_MOD0);
        value1[index] = (uint32_t)(((uint64_t)value1[index] * value1[index]) % XRAY_BIGINT_NTT16_MOD1);
        value2[index] = (uint32_t)(((uint64_t)value2[index] * value2[index]) % XRAY_BIGINT_NTT32_MOD2);
      }
    }
  }
  if (ok) {
    XrayNtt16TransformTask inverse_tasks[] = {
      {value0, transform_length, XRAY_BIGINT_NTT16_MOD0, 1, 0},
      {value1, transform_length, XRAY_BIGINT_NTT16_MOD1, 1, 0},
      {value2, transform_length, XRAY_BIGINT_NTT32_MOD2, 1, 0}
    };
    ok =
      ntt16_transform_tasks_select(inverse_tasks, sizeof(inverse_tasks) / sizeof(inverse_tasks[0]), persistent_transforms) &&
      (direct_crt ?
      ntt32_reconstruct_to_bigint_direct(
        out,
        value0,
        value1,
        value2,
        coefficient_count,
        digit_count,
        digit_count) :
      ntt32_reconstruct_to_bigint(
        out,
        value0,
        value1,
        value2,
        coefficient_count,
        digit_count,
        digit_count,
        coefficient_low,
        coefficient_high,
        coefficient_capacity));
    }

  if (!using_workspace) {
    free(value0);
    free(value1);
    free(value2);
  }
  return ok;
}

static int square_ntt32_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  return square_ntt32_probe_internal_mode(out, value, 0, 0, 0, 0, 0);
}

int xray_bigint_square_ntt32_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal(&temp, value);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal(out, value);
}

int xray_bigint_square_ntt32_reuse_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 1, 0, 0, 0, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 1, 0, 0, 0, 0);
}

int xray_bigint_square_ntt32_pointwise_parallel_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 0, 1, 0, 0, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 0, 1, 0, 0, 0);
}

int xray_bigint_square_ntt32_persistent_transform_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 0, 0, 1, 0, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 0, 0, 1, 0, 0);
}

int xray_bigint_square_ntt32_reuse_persistent_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 1, 0, 1, 0, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 1, 0, 1, 0, 0);
}

int xray_bigint_square_ntt32_tailmap_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 0, 0, 0, 1, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 0, 0, 0, 1, 0);
}

int xray_bigint_square_ntt32_lowtailmap_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 0, 0, 0, 2, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 0, 0, 0, 2, 0);
}

int xray_bigint_square_ntt32_direct_crt_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_ntt32_probe_internal_mode(&temp, value, 0, 0, 0, 0, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_ntt32_probe_internal_mode(out, value, 0, 0, 0, 0, 1);
}

int xray_bigint_mul_sparse_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t left_nonzero = count_nonzero_limbs(left);
  size_t right_nonzero = count_nonzero_limbs(right);
  if (left_nonzero == 0 || right_nonzero == 0) return set_u32(out, 0);
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_schoolbook_sparse(&temp, left, right, left_nonzero, right_nonzero);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_schoolbook_sparse(out, left, right, left_nonzero, right_nonzero);
}

int xray_bigint_mul_karatsuba_sum_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_sum_mode(&temp, left, right, active_threshold, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_sum_mode(out, left, right, active_threshold, 0);
}

int xray_bigint_mul_toom3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 0, 1, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 0, 1, 0);
}

int xray_bigint_mul_toom3_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 1, 1, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 1, 1, 0);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 1, active_depth, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 1, active_depth, 0);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_view_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 1, active_depth, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 1, active_depth, 1);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_workspace_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_workspace_probe_internal(out, left, right, active_threshold, active_depth, 0);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, 0);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_div2_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, XRAY_TOOM3_INTERP_SHIFT_DIV2);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, XRAY_TOOM3_INTERP_SHIFT_DIV2);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_div3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, XRAY_TOOM3_INTERP_EXACT_DIV3);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, XRAY_TOOM3_INTERP_EXACT_DIV3);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_div2_div3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_inplace_div2_div3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM3_INTERP_INPLACE_DIV;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_square_toom3_full_workspace_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, 0);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

void xray_bigint_mul_workspace_init(XrayBigIntMulWorkspace *workspace) {
  if (!workspace) return;
  workspace->toom3_frames = NULL;
  workspace->toom3_frame_count = 0;
  workspace->karatsuba_frames = NULL;
  workspace->karatsuba_frame_count = 0;
}

void xray_bigint_mul_workspace_clear(XrayBigIntMulWorkspace *workspace) {
  if (!workspace) return;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  XrayToom3Workspace toom_workspace;
  XrayKaratsubaWorkspace karatsuba_workspace;
  toom_workspace.frames = (XrayToom3WorkspaceFrame *)workspace->toom3_frames;
  toom_workspace.frame_count = workspace->toom3_frame_count;
  karatsuba_workspace.frames = (XrayKaratsubaWorkspaceFrame *)workspace->karatsuba_frames;
  karatsuba_workspace.frame_count = workspace->karatsuba_frame_count;
  toom3_workspace_clear(&toom_workspace);
  karatsuba_workspace_clear(&karatsuba_workspace);
#else
  free(workspace->toom3_frames);
  free(workspace->karatsuba_frames);
#endif
  workspace->toom3_frames = NULL;
  workspace->toom3_frame_count = 0;
  workspace->karatsuba_frames = NULL;
  workspace->karatsuba_frame_count = 0;
}

int xray_bigint_square_toom3_full_workspace_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_reuse_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, workspace, 0, 0U, 0U);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_reuse_probe_internal(out, value, active_threshold, active_depth, interp_flags, workspace, 0, 0U, 0U);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom3_full_workspace_reuse_neg2_div2_div3_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM3_INTERP_EVAL_NEG2;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_reuse_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, workspace, 0, 0U, 0U);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_reuse_probe_internal(out, value, active_threshold, active_depth, interp_flags, workspace, 0, 0U, 0U);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom3_full_workspace_reuse_parallel_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t parallel_min_limbs,
  size_t parallel_group_count,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  size_t active_parallel_min = parallel_min_limbs >= 1U ? parallel_min_limbs : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_depth,
      interp_flags,
      workspace,
      0,
      active_parallel_min,
      parallel_group_count);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_depth,
    interp_flags,
    workspace,
    0,
    active_parallel_min,
    parallel_group_count);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)parallel_min_limbs;
  (void)parallel_group_count;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom3_direct_coeff_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_direct_coeff_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_depth,
      interp_flags,
      workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_direct_coeff_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_depth,
    interp_flags,
    workspace);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom3_chung_sqr3_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_chung_sqr3_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_depth,
      interp_flags,
      workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_chung_sqr3_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_depth,
    interp_flags,
    workspace);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom4_chung_sqr4_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_threshold,
      active_depth,
      interp_flags,
      0U,
      workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_threshold,
    active_depth,
    interp_flags,
    0U,
    workspace);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom4_chung_sqr4_split_leaf_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t top_leaf_threshold,
  size_t child_leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_top_threshold = top_leaf_threshold >= 2U ? top_leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_child_threshold = child_leaf_threshold >= 2U ? child_leaf_threshold : active_top_threshold;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_top_threshold,
      active_child_threshold,
      active_depth,
      interp_flags,
      0U,
      workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
    out,
    value,
    active_top_threshold,
    active_child_threshold,
    active_depth,
    interp_flags,
    0U,
    workspace);
#else
  (void)out;
  (void)value;
  (void)top_leaf_threshold;
  (void)child_leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom4_chung_sqr4_parallel_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t parallel_group_count,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  size_t active_groups = parallel_group_count >= 1U ? parallel_group_count : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_threshold,
      active_depth,
      interp_flags,
      active_groups,
      workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_chung_sqr4_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_threshold,
    active_depth,
    interp_flags,
    active_groups,
    workspace);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)parallel_group_count;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom3_direct_coeff_parallel_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t parallel_group_count,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_direct_coeff_parallel_full_workspace_reuse_probe_internal(
      &temp,
      value,
      active_threshold,
      active_depth,
      interp_flags,
      workspace,
      parallel_group_count);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_direct_coeff_parallel_full_workspace_reuse_probe_internal(
    out,
    value,
    active_threshold,
    active_depth,
    interp_flags,
    workspace,
    parallel_group_count);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)parallel_group_count;
  (void)workspace;
  return 0;
#endif
}

static int square_toom3_direct_coeff_dispatch_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);

  size_t split = (value->count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2;
  view_bigint_slice(&a0, value, 0, split);
  view_bigint_slice(&a1, value, split, split);
  view_bigint_slice(&a2, value, split * 2U, value->count > split * 2U ? value->count - split * 2U : 0);

  XrayScratchBigInt c0, c1, c2, c3, c4, twice_a0a2;
  xray_bigint_init(&c0);
  xray_bigint_init(&c1);
  xray_bigint_init(&c2);
  xray_bigint_init(&c3);
  xray_bigint_init(&c4);
  xray_bigint_init(&twice_a0a2);

  int ok = xray_bigint_square(&c0, &a0) &&
    xray_bigint_square(&c2, &a1) &&
    xray_bigint_square(&c4, &a2) &&
    xray_bigint_mul(&c1, &a0, &a1) &&
    xray_bigint_mul(&twice_a0a2, &a0, &a2) &&
    xray_bigint_mul(&c3, &a1, &a2) &&
    xray_bigint_add(&c1, &c1, &c1) &&
    xray_bigint_add(&twice_a0a2, &twice_a0a2, &twice_a0a2) &&
    xray_bigint_add(&c2, &c2, &twice_a0a2) &&
    xray_bigint_add(&c3, &c3, &c3);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 4U) &&
      add_shifted_inplace(out, &c0, 0) &&
      add_shifted_inplace(out, &c1, split) &&
      add_shifted_inplace(out, &c2, split * 2U) &&
      add_shifted_inplace(out, &c3, split * 3U) &&
      add_shifted_inplace(out, &c4, split * 4U);
    if (ok) normalize(out);
  }

  xray_bigint_clear(&twice_a0a2);
  xray_bigint_clear(&c4);
  xray_bigint_clear(&c3);
  xray_bigint_clear(&c2);
  xray_bigint_clear(&c1);
  xray_bigint_clear(&c0);
  return ok;
}

int xray_bigint_square_toom3_direct_coeff_dispatch_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_direct_coeff_dispatch_probe_internal(&temp, value);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_direct_coeff_dispatch_probe_internal(out, value);
}

int xray_bigint_square_toom3_full_workspace_mul_points_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, 1);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_square_toom3_full_workspace_reuse_mul_points_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom3_full_workspace_reuse_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, workspace, 1, 0U, 0U);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom3_full_workspace_reuse_probe_internal(out, value, active_threshold, active_depth, interp_flags, workspace, 1, 0U, 0U);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_reuse_div2_div3_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_reuse_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_reuse_neg2_div2_div3_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM3_INTERP_EVAL_NEG2;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_reuse_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_full_workspace_reuse_inplace_div2_div3_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM3_INTERP_INPLACE_DIV;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_full_workspace_reuse_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom4_top_full_workspace_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_top_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, NULL, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_top_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, NULL, 0);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_square_toom4_top_full_workspace_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_top_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, workspace, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_top_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, workspace, 0);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_square_toom4_top_full_workspace_mul_points_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_top_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, NULL, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_top_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, NULL, 1);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_square_toom4_top_full_workspace_reuse_mul_points_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *value,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !value || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_toom4_top_full_workspace_probe_internal(&temp, value, active_threshold, active_depth, interp_flags, workspace, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_toom4_top_full_workspace_probe_internal(out, value, active_threshold, active_depth, interp_flags, workspace, 1);
#else
  (void)out;
  (void)value;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom4_top_full_workspace_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom4_top_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, NULL);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom4_top_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, NULL);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_toom4_top_full_workspace_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom4_top_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom4_top_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom4_top_full_workspace_reuse_factored_div_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom4_top_full_workspace_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom4_top_full_workspace_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom4_top_full_workspace_reuse_factored_div_parallel_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t parallel_min_limbs,
  size_t parallel_group_count,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  size_t active_parallel_min = parallel_min_limbs >= 1U ? parallel_min_limbs : 1800U;
  size_t active_parallel_groups = parallel_group_count;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom4_top_full_workspace_probe_internal_ex(
      &temp,
      left,
      right,
      active_threshold,
      active_depth,
      interp_flags,
      workspace,
      active_parallel_min,
      active_parallel_groups);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom4_top_full_workspace_probe_internal_ex(
    out,
    left,
    right,
    active_threshold,
    active_depth,
    interp_flags,
    workspace,
    active_parallel_min,
    active_parallel_groups);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)parallel_min_limbs;
  (void)parallel_group_count;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom5_top_full_workspace_reuse_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 | XRAY_TOOM3_INTERP_EXACT_DIV3;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom5_top_full_workspace_reuse_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom5_top_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_toom5_top_full_workspace_reuse_factored_div_probe(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  size_t depth_limit,
  XrayBigIntMulWorkspace *workspace) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right || !workspace) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : 48U;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  unsigned int interp_flags = XRAY_TOOM3_INTERP_SHIFT_DIV2 |
    XRAY_TOOM3_INTERP_EXACT_DIV3 |
    XRAY_TOOM4_INTERP_FACTORED_DIV;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom5_top_full_workspace_reuse_probe_internal(&temp, left, right, active_threshold, active_depth, interp_flags, workspace);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom5_top_full_workspace_reuse_probe_internal(out, left, right, active_threshold, active_depth, interp_flags, workspace);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  (void)workspace;
  return 0;
#endif
}

int xray_bigint_mul_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_mode(&temp, left, right, active_threshold, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_mode(out, left, right, active_threshold, 1);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  return 0;
#endif
}

int xray_bigint_mul_dense_leaf_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  int use_unroll4 = 1;
#else
  int use_unroll4 = 0;
#endif
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_mode_ex(&temp, left, right, active_threshold, use_unroll4, 0, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_mode_ex(out, left, right, active_threshold, use_unroll4, 0, 0);
}

int xray_bigint_mul_karatsuba_view_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  int use_unroll4 = 1;
#else
  int use_unroll4 = 0;
#endif
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_mode_ex(&temp, left, right, active_threshold, use_unroll4, 1, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_mode_ex(out, left, right, active_threshold, use_unroll4, 1, 1);
}

int xray_bigint_mul_karatsuba_workspace_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_karatsuba_workspace_probe_internal(&temp, left, right, active_threshold);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_karatsuba_workspace_probe_internal(out, left, right, active_threshold);
}

static uint32_t mod_u32_with_reciprocal(const XrayScratchBigInt *value, uint32_t modulus, uint64_t reciprocal) {
  uint32_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    divmod_word_u32(remainder, value->limbs[index], modulus, reciprocal, use_high_half, &remainder);
  }
  return remainder;
}

uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus) {
  if (!value || modulus == 0) return 0;
  if (modulus == XRAY_BIGINT_FERMAT_65537) return mod_65537_folded(value);
  return mod_u32_with_reciprocal(value, modulus, reciprocal_u32(modulus));
}

uint32_t xray_bigint_mod_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context) {
  if (!value || !context || context->modulus == 0) return 0;
  if (context->use_fermat_65537) return mod_65537_folded(value);
  return mod_u32_with_reciprocal(value, context->modulus, context->reciprocal);
}

int xray_bigint_divmod_u32(XrayScratchBigInt *quotient, uint32_t *remainder, const XrayScratchBigInt *value, uint32_t divisor) {
  if (!quotient || !value || divisor == 0) return 0;
  if (!reserve_limbs(quotient, value->count ? value->count : 1)) return 0;
  uint32_t rem = 0;
  uint64_t reciprocal = reciprocal_u32(divisor);
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    quotient->limbs[index] = divmod_word_u32(rem, value->limbs[index], divisor, reciprocal, use_high_half, &rem);
  }
  quotient->count = value->count;
  normalize(quotient);
  if (remainder) *remainder = rem;
  return 1;
}

static uint32_t gcd_u32(uint32_t a, uint32_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned long shift = 0;
  unsigned long a_shift = 0;
  unsigned long b_shift = 0;
  _BitScanForward(&shift, a | b);
  _BitScanForward(&a_shift, a);
  a >>= a_shift;
  do {
    _BitScanForward(&b_shift, b);
    b >>= b_shift;
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#elif defined(__GNUC__) || defined(__clang__)
  unsigned int shift = (unsigned int)__builtin_ctz(a | b);
  a >>= (unsigned int)__builtin_ctz(a);
  do {
    b >>= (unsigned int)__builtin_ctz(b);
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#else
  unsigned int shift = 0;
  while (((a | b) & 1U) == 0) {
    a >>= 1U;
    b >>= 1U;
    shift++;
  }
  while ((a & 1U) == 0) a >>= 1U;
  do {
    while ((b & 1U) == 0) b >>= 1U;
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#endif
}

uint32_t xray_bigint_gcd_u32(const XrayScratchBigInt *value, uint32_t other) {
  if (other == 0) return 0;
  return gcd_u32(xray_bigint_mod_u32(value, other), other);
}

uint32_t xray_bigint_gcd_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context) {
  if (!context || context->modulus == 0) return 0;
  return gcd_u32(xray_bigint_mod_u32_precomputed(value, context), context->modulus);
}

uint32_t xray_bigint_powmod_u32(const XrayScratchBigInt *base, uint32_t exponent, uint32_t modulus) {
  if (!base || modulus == 0) return 0;
  uint64_t result = 1 % modulus;
  uint64_t factor = xray_bigint_mod_u32(base, modulus);
  uint32_t power = exponent;
  while (power) {
    if (power & 1U) result = (result * factor) % modulus;
    factor = (factor * factor) % modulus;
    power >>= 1;
  }
  return (uint32_t)result;
}

uint32_t xray_bigint_powmod_u32_precomputed(const XrayScratchBigInt *base, uint32_t exponent, const XrayBigIntU32ModContext *context) {
  if (!base || !context || context->modulus == 0) return 0;
  uint32_t modulus = context->modulus;
  uint64_t result = 1 % modulus;
  uint64_t factor = xray_bigint_mod_u32_precomputed(base, context);
  uint32_t power = exponent;
  while (power) {
    if (power & 1U) result = (result * factor) % modulus;
    factor = (factor * factor) % modulus;
    power >>= 1;
  }
  return (uint32_t)result;
}
