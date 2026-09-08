#include "dvar_patches.h"
#include "game.h"
#include "state.h"
#include "logger.h"
#include "inline_hook.h"
#include "safemem.h"
#include "dvar_names.h"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace dvars {
namespace {
constexpr size_t kCapacity = 128;

struct Entry {
    std::atomic<uint32_t> live{0}; // 0 = empty, 1 = active
    char key[32]{};                // runtime token to match
    char label[80]{};              // human-readable name
    std::atomic<uint8_t> forced{0};
    std::atomic<uint8_t> enabled{0};
    std::atomic<uint32_t> hits{0};
    std::atomic<void*> dvar{nullptr};
};

Entry g_table[kCapacity];
SRWLOCK g_addLock = SRWLOCK_INIT;

std::atomic<game::Dvar_RegisterBool_t> g_original{nullptr};
std::atomic<game::Dvar_RegisterVariant_t> g_variantOriginal{nullptr};

// token -> readable name (binary search over the generated table).
const char* ResolveToken(const char* token) {
    if (!token || !*token)
        return nullptr;
    int lo = 0, hi = dvardb::kCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = std::strcmp(token, dvardb::kEntries[mid].token);
        if (c == 0)
            return dvardb::kEntries[mid].name;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return nullptr;
}

Entry* FindByName(const char* nm) {
    if (!nm || !*nm)
        return nullptr;
    for (auto& e : g_table) {
        if (e.live.load(std::memory_order_acquire) != 1)
            continue;
        if ((e.key[0] && std::strcmp(e.key, nm) == 0) || std::strcmp(e.label, nm) == 0)
            return &e;
    }
    return nullptr;
}

Entry* Allocate(const char* token, const char* label, bool forced, bool enabled) {
    AcquireSRWLockExclusive(&g_addLock);
    Entry* slot = nullptr;
    for (auto& e : g_table)
        if (e.live.load(std::memory_order_relaxed) == 0) {
            slot = &e;
            break;
        }
    if (slot) {
        strncpy_s(slot->key, sizeof(slot->key), token ? token : "", _TRUNCATE);
        strncpy_s(slot->label, sizeof(slot->label), label ? label : "", _TRUNCATE);
        slot->forced.store(forced ? 1 : 0, std::memory_order_relaxed);
        slot->enabled.store(enabled ? 1 : 0, std::memory_order_relaxed);
        slot->hits.store(0, std::memory_order_relaxed);
        slot->dvar.store(nullptr, std::memory_order_relaxed);
        slot->live.store(1, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_addLock);
    return slot;
}

// Override an already-forced default (or add a new one). kDefaults calls Allocate first; FindByName
// returns the FIRST live entry, so a second Allocate of the same token would be shadowed -> update the
// existing entry in place. Used by the online-MP route block to flip values kDefaults already set.
void SetForced(const char* token, const char* label, bool value) {
    Entry* e = FindByName(label);
    if (e) {
        e->forced.store(value ? 1 : 0, std::memory_order_relaxed);
        e->enabled.store(1, std::memory_order_relaxed);
    } else {
        Allocate(token, label, value, /*enabled*/ true);
    }
}

void FormatValue(int type, void* pValue, char* out, size_t n) {
    if (!pValue) {
        _snprintf_s(out, n, _TRUNCATE, "?");
        return;
    }
    if (type == 0) {
        uint8_t b;
        if (safemem::ReadBytes(pValue, &b, 1))
            _snprintf_s(out, n, _TRUNCATE, "%s", b ? "true" : "false");
        else
            _snprintf_s(out, n, _TRUNCATE, "?");
        return;
    }
    if (type == 1) {
        float f;
        if (safemem::ReadBytes(pValue, &f, 4))
            _snprintf_s(out, n, _TRUNCATE, "%g", f);
        else
            _snprintf_s(out, n, _TRUNCATE, "?");
        return;
    }
    uint32_t raw;
    if (safemem::ReadBytes(pValue, &raw, 4))
        _snprintf_s(out, n, _TRUNCATE, "0x%08x", raw);
    else
        _snprintf_s(out, n, _TRUNCATE, "?");
}

void* __fastcall Detour(const char* name, bool value, unsigned int flags, const char* desc) {
    state::boolDvarsSeen.fetch_add(1, std::memory_order_relaxed);
    char nm[96];
    safemem::ReadString(name, nm, sizeof(nm));

    bool newValue = value;
    Entry* e = nm[0] ? FindByName(nm) : nullptr;
    if (e && e->enabled.load(std::memory_order_relaxed))
        newValue = e->forced.load(std::memory_order_relaxed) != 0;

    void* result = g_original.load(std::memory_order_acquire)(name, newValue, flags, desc);

    if (e) {
        e->dvar.store(result, std::memory_order_relaxed);
        e->hits.fetch_add(1, std::memory_order_relaxed);
        if (e->enabled.load(std::memory_order_relaxed))
            LOG_INFO("Patches/Dvar_RegisterBool", "Patched '%s' -> %s", e->label,
                     newValue ? "true" : "false");
    }
    return result;
}

// Tracer: fires for every dvar of every type. Resolves the token to a name.
void* __fastcall VariantDetour(const char* name,
                               unsigned int checksum,
                               int type,
                               unsigned int flags,
                               void* pValue,
                               void* pDomain,
                               const char* desc) {
    state::dvarsSeen.fetch_add(1, std::memory_order_relaxed);

    char nm[96];
    safemem::ReadString(name, nm, sizeof(nm));
    const char* human = ResolveToken(nm);

    char val[96];
    FormatValue(type, pValue, val, sizeof(val));

    const char* tn = game::DvarTypeName(type);
    const char* shown = human ? human : (nm[0] ? nm : "(?)");
    if (tn)
        logger::Trace("dvar", "%-46s %-6s = %-22s  tok=%-12s  0x%08X", shown, tn, val, nm,
                      checksum);
    else
        logger::Trace("dvar", "%-46s type%-2d= %-22s  tok=%-12s  0x%08X", shown, type, val, nm,
                      checksum);

    return g_variantOriginal.load(std::memory_order_acquire)(name, checksum, type, flags, pValue,
                                                             pDomain, desc);
}

bool PrologueMatches(const uint8_t* target, const uint8_t* expected, size_t len) {
    uint8_t buf[32];
    if (len > sizeof(buf))
        return false;
    if (!safemem::ReadBytes(target, buf, len))
        return false;
    return std::memcmp(buf, expected, len) == 0;
}

struct Default {
    const char* token;
    const char* name;
    bool value;
};
constexpr Default kDefaults[] = {
    {"MLNMPQOON", "cg_viewedSplashScreen", true},
    {"MTSTMKPMRM", "ui_onlineRequired", false},
    {"LPNMMPKRL", "com_lan_lobby_enabled", true},
    {"RLSPOOTTT", "com_checkIfGameModeInstalled", false},
    {"MROLPRPTPO", "com_force_premium", true},
    {"LSTQOKLTRN", "force_offline_menus", true},
    // Target 1.20 hash table and IW8-1.20 reference confirm this companion switch.
    {"MPSSOTQQPM", "force_offline_enabled", true},
    {"OKLQKPPKPQ", "con_bindableGrave", false},
    {"MOQQSONTOT", "con_restricted", false},
    {"NRNSMTPL", "com_hide_console", false},
    {"LKSQOLNKLP", "lui_skip_boot_flow", true},
    {"LMMRONPQMO", "lui_force_online_menus", false},
    {"LNTOKPTKS", "lui_cod_points_enabled", false},
    {"LSSRRSMNMR", "lui_dev_features_enabled", true},
    {"LRKPTLNQTT", "lui_enable_magma_blade_layout", false},
    {"LSPSKLPNQT", "lui_wz_tutorial_optional", true},
    {"", "lui_enable_frontend_seasonal_content", true}, // not in 1.24 dump; match by name
    {"LTOQRQMMLQ", "online_lan_cross_play", true},
    {"NTTRLOPQKS", "xp_dec_dc", false},
    {"NSPPTONLNP", "online_blueprints_enabled", false},
    {"LPSPMQSNPQ", "systemlink", true},
    {"LLPNKKORPT", "systemlink_host", true},
    // Hinatyu/1.67 reference forces this true (with force_offline_menus + lui_dev_features_enabled) —
    // "logged in to Xbox Live" flag some offline fences read. Harmless; completes the reference recipe.
    {"LLOKQOSPPP", "xblive_loggedin", true},
    // CONDITIONS.ShouldCheckDLC() = Dvar.GetBool("LKSTRMKTML"); force FALSE so the offline MP
    // mode-fence skips the (erroneously-failing offline) "missing MP DLC packs" check.
    {"LKSTRMKTML", "should_check_dlc", false},

    // force-unlock all camos/cosmetics/attachments (the game's own unlock-all switches)
    {"OLKMKMTKRO", "unlockAllItems", true},
    {"MNLPOPMMSK", "force_unlock_all_attachments", true},
    {"LSPQSSPSOL", "force_unlock_all_attachment_lines", true},
    {"NQRLNKMTSL", "force_unlock_all_killstreaks", true},
};
} // namespace

const char* ResolveName(const char* token) {
    return ResolveToken(token);
}

void InitDefaults(bool onlineMpRoute, bool luiForceOnline) {
    for (const auto& d : kDefaults)
        Allocate(d.token, d.name, d.value, /*enabled*/ true);

    if (onlineMpRoute) {

        SetForced("LMMRONPQMO", "lui_force_online_menus", luiForceOnline);
        SetForced("LSTQOKLTRN", "force_offline_menus", false);
        SetForced("LPSPMQSNPQ", "systemlink", false);
        SetForced("LLPNKKORPT", "systemlink_host", false);
        // -- let the online UI work WITHOUT a live service connection --
        SetForced("LMMRKMKSOR", "net_require_demonware", false);
        SetForced("NOSONNPTLM", "online_auth_skip_auth", true);
        SetForced("MTSTMKPMRM", "ui_onlineRequired", false);
        SetForced("OLMKQPQOM", "online_anticheat_should_com_error_if_mp_or_cp_banned", false);
        SetForced("LNSPMQMSS", "online_anticheat_should_main_menu_fence_fail_if_mp_banned", false);
        // -- kill phone-home (belt & suspenders; a live-service connect is already gated off above) --
        SetForced("NKLQKPPOMR", "dlog_enabled", false);
        SetForced("OLMLSQPSLM", "demonware_presence_notifications_enabled", false);
        SetForced("NQKTTMLOKP", "dw_leaderboard_write_active", false);
        SetForced("PLNPOMOPR", "online_analytics_streamer_enabled", false);
        SetForced("MKTLQRMTTS", "online_analytics_streamer_should_be_enabled_based_on_package",
                  false);
        SetForced("PQTNSQLKP", "online_archive_streamer_enabled", false);
        SetForced("OLPRPQSPNN", "online_archive_streamer_force", false);
        SetForced("LOLQRSKLRS", "content_download_should_auto_download", false);
        SetForced("LQTMQOTTTQ", "online_quartermaster_instrumentation_enabled", false);
        SetForced("OLRSMNPLNK", "cl_inhibit_stats_upload", true);
        // Custom Games button visibility: PrivateMatchButton is gated SOLELY on this dvar
        // (CONDITIONS.IsMPPrivateMatchEnabled = Dvar LQKTNLONLP, MPPlayMenuButtons.lua:414). Force it on so
        // the button shows in the offline online-MP menu. (No online/privilege gate on visibility.)
        SetForced("LQKTNLONLP", "ui_mp_private_match_enabled", true);
        LOG_INFO("Patches",
                 "online-MP route ON: UI-online dvars forced on, direct-service dvars off");
    }

    LOG_INFO("Patches", "loaded %zu built-in overrides%s; %d dvar names embedded",
             sizeof(kDefaults) / sizeof(kDefaults[0]), onlineMpRoute ? " (+online-MP route)" : "",
             dvardb::kCount);
}

hook::Status InstallHook(uintptr_t moduleBase, bool hookBool, bool hookVariant) {
    if (hookBool) {
        const bool already = g_original.load(std::memory_order_acquire) != nullptr;
        auto* target = reinterpret_cast<void*>(moduleBase + game::kDvarRegisterBoolRVA);
        const auto status = hook::Install(target, &Detour, game::kDvarRegisterBoolPrologue,
                                          game::kDvarRegisterBoolStolen, g_original);
        if (status != hook::Status::Installed)
            return status;
        if (!already)
            LOG_INFO("Patches",
                     "Dvar_RegisterBool installed at RVA 0x%llX (original published before enable)",
                     (unsigned long long)game::kDvarRegisterBoolRVA);
    }
    if (hookVariant) {
        const bool already = g_variantOriginal.load(std::memory_order_acquire) != nullptr;
        auto* target = reinterpret_cast<void*>(moduleBase + game::kDvarRegisterVariantRVA);
        const auto status =
            hook::Install(target, &VariantDetour, game::kDvarRegisterVariantPrologue,
                          game::kDvarRegisterVariantStolen, g_variantOriginal);
        if (status != hook::Status::Installed)
            return status;
        state::variantHooked.store(true);
        if (!already)
            LOG_INFO("Patches", "Dvar_RegisterVariant trace installed at RVA 0x%llX",
                     (unsigned long long)game::kDvarRegisterVariantRVA);
    }
    return hook::Status::Installed;
}
}
