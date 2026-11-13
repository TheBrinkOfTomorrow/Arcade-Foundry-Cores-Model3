// Each Model 3 game's core version, x.y.z (afcore_api.core_version; what the
// numbers mean is in include/afcore.h). Bump a game's line in the same commit
// as a change to how it plays or saves (its core_version in games.toml, then
// Scripts/gen_games.py); a change to the shared machine bumps
// every game it reaches. 1.0.0: the first versioned release (afcore 5.0.0).
#pragma once

// GAMES:BEGIN versions (Scripts/gen_games.py writes these from games.toml)
#define M3CORE_VERSION_vf3 AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_scud AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_scudplus AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_daytona2 AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_dayto2pe AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_srally2 AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_lemans24 AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_dirtdvls AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_harley AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_ecau AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_fvipers2 AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_von2 AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_spikeout AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_spikeofe AFCORE_MAKE_VERSION(1, 2, 0)
#define M3CORE_VERSION_vs299a AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_swtrilgy AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_lostwsga AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_lamachin AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_oceanhuna AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_magtruck AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_bassdx AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_getbass AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_getbassur AFCORE_MAKE_VERSION(1, 1, 0)
#define M3CORE_VERSION_getbassdx AFCORE_MAKE_VERSION(1, 1, 0)
// GAMES:END versions

#define M3CORE_VERSION_OF_(set) M3CORE_VERSION_##set
#define M3CORE_VERSION_OF(set) M3CORE_VERSION_OF_(set)
