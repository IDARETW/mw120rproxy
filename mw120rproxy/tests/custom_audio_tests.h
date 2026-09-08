bool audioNativeSuccess = false;
unsigned audioGrabCalls = 0, audioPlayed = 0;
unsigned char audioSurface = 0;
void AudioPick(const void*,
               const void*,
               const void** first,
               const void** second,
               float* blend,
               unsigned a,
               unsigned b,
               unsigned c,
               unsigned d) {
    Check(a == 1 && b == 2 && c == 3 && d == 4, "sound picker preserves four context arguments");
    *first = audioNativeSuccess ? reinterpret_cast<void*>(99) : nullptr;
    if (second)
        *second = nullptr;
    if (blend)
        *blend = 0;
}
void AudioGrab(int client, int entity, bool left, unsigned char surface) {
    Check(client == 0 && entity == 0 && left, "ladder sound preserves client/entity/hand");
    ++audioGrabCalls;
    audioSurface = surface;
}
struct AudioAlias {
    const char* name;
    unsigned id;
} audioAlias{"step_default_plr_walk_wood_ladder_left", 44};
void* AudioFind(const char* name) {
    Check(!strcmp(name, audioAlias.name), "missing grab cache resolves resident wood ladder sound");
    return &audioAlias;
}
void AudioPlay(unsigned id, int client, int entity, const float* origin) {
    Check(id == 44 && client == 0 && entity == 0 && origin[0] == 12,
          "ladder fallback preserves positional native sound arguments");
    ++audioPlayed;
}
void AudioTests() {
    for (const auto* b : {&replay::SoundAliasByName, &replay::SoundAtPosition,
                          &replay::PickSoundAlias, &replay::LadderGrabSound}) {
        Commit(b->rva);
        memcpy(image + b->rva, b->bytes, b->size);
    }
    PrologueFixture(replay::PickSoundAlias, {0x41, 0x5F, 0x41, 0x5C, 0x5F, 0x5B, 0x5D},
                    reinterpret_cast<void*>(&AudioPick));
    PrologueFixture(replay::LadderGrabSound, {0x41, 0x5C, 0x5F, 0x5E, 0x5D},
                    reinterpret_cast<void*>(&AudioGrab));
    Check(customaudio::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "native audio hooks validate both Replay prologues");
    using Picker = void (*)(const void*, const void*, const void**, const void**, float*, unsigned,
                            unsigned, unsigned, unsigned);
    auto pick = reinterpret_cast<Picker>(image + replay::PickSoundAlias.rva);
    std::array<unsigned char, 0xE8 * 3> entries{};
    uint64_t carpet = 1ull << 3, concrete = 1ull << 5;
    memcpy(entries.data() + 0x40, &carpet, 8);
    memcpy(entries.data() + 0xE8 + 0x40, &carpet, 8);
    memcpy(entries.data() + 0x1D0 + 0x40, &concrete, 8);
    struct List {
        const char* name;
        uint64_t id;
        void* entries;
        int count, pad;
    } list{"step_default_plr_walk_left", 1, entries.data(), 3, 0};
    std::array<unsigned char, 0x80> params{};
    int surface = 3;
    memcpy(params.data() + 0x64, &surface, 4);
    const auto original = params;
    const void* first = nullptr;
    const void* second = nullptr;
    float blend = 0;
    pick(&list, params.data(), &first, &second, &blend, 1, 2, 3, 4);
    const auto one = first;
    Check(first == entries.data() || first == entries.data() + 0xE8,
          "failed carpet selection chooses an authored carpet variant");
    pick(&list, params.data(), &first, &second, &blend, 1, 2, 3, 4);
    Check(first != one, "fallback alternates available carpet variants");
    surface = 63;
    memcpy(params.data() + 0x64, &surface, 4);
    pick(&list, params.data(), &first, &second, &blend, 1, 2, 3, 4);
    Check(first == entries.data() + 0x1D0, "unsupported surface gets concrete fallback");
    audioNativeSuccess = true;
    pick(&list, params.data(), &first, &second, &blend, 1, 2, 3, 4);
    Check(first == reinterpret_cast<void*>(99),
          "successful native selection is never duplicated or replaced");
    audioNativeSuccess = false;
    list.name = "weapon_fire";
    pick(&list, params.data(), &first, &second, &blend, 1, 2, 3, 4);
    Check(!first, "nonmovement sound contexts remain native");
    surface = 3;
    memcpy(params.data() + 0x64, &surface, 4);
    Check(params == original, "sound fallback leaves native play parameters unchanged");
    // Grab fallback is event driven: one call produces one sound, populated cache
    // produces none, and the native event still runs exactly once.
    for (auto r : {uintptr_t(0xF276578), uintptr_t(0xBC20F00)})
        Commit(r);
    uintptr_t zero = 0;
    memcpy(image + 0xF276578, &zero, 8);
    std::array<unsigned char, 0x160> entity{};
    std::array<unsigned char, 0x50> player{};
    auto* ep = entity.data();
    auto* pp = player.data();
    memcpy(image + 0xBC20F00, &ep, 8);
    memcpy(entity.data() + 0x150, &pp, 8);
    float x = 12;
    memcpy(player.data() + 0x30, &x, 4);
    Jump(image + replay::SoundAliasByName.rva, reinterpret_cast<void*>(&AudioFind));
    Jump(image + replay::SoundAtPosition.rva, reinterpret_cast<void*>(&AudioPlay));
    auto grab = reinterpret_cast<void (*)(int, int, bool, unsigned char)>(
        image + replay::LadderGrabSound.rva);
    grab(0, 0, true, 0);
    Check(audioGrabCalls == 1 && audioSurface == 22 && audioPlayed == 1,
          "missing ladder cache gets one sound at a native grab event");
    uintptr_t cached = 1;
    memcpy(image + 0xF276578, &cached, 8);
    grab(0, 0, true, 13);
    Check(audioGrabCalls == 2 && audioSurface == 13 && audioPlayed == 1,
          "existing ladder cache and typed metal sound are preserved");
    memcpy(image + 0xBC20F00, &zero, 8);
    memcpy(image + 0xF276578, &zero, 8);
    puts(
        "PASS: native audio hook ABI, carpet variants, concrete fallback, successful/nonmovement passthrough and event-driven ladder audio without duplication");
}
