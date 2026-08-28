#pragma once

#include <optional>
#include <string>
#include <vector>

// Finds "<prefix>_NNNN" (NNNN = 0000-9999, zero-padded) for the smallest
// NNNN such that no file "<prefix>_NNNN<ext>" exists on disk, for any ext
// in `extensions`. Passing multiple extensions together keeps a set of
// sibling output files (e.g. this module's raw dump, and eventually a
// BH-format .spc/.sdt/.json triplet) sharing one index instead of drifting
// independently. Returns std::nullopt if no free index was found in
// [0, 10000).
//
// Not race-free against a second concurrent caller -- fine given this
// module only ever runs one acquisition at a time, matching the same
// assumption made by OpenScan-BH_SPC's own UniqueFileName (src/
// UniqueFileName.c there), which this is ported from (using
// std::filesystem instead of Shlwapi's PathFileExistsA, otherwise the same
// scheme).
//
// TODO: linear-scan-plus-existence-check was chosen here specifically to
// match OpenScan-BH_SPC's own scheme, since a project goal is eventually
// producing BH-compatible output files (see EventPipeline.cpp's raw-dump
// use of this). Worth reconsidering for anything that isn't trying to
// match that specific format/tooling -- e.g. a timestamp-based name avoids
// the O(n) scan and the 10000 cap, and embeds useful metadata (when) for
// free, at the cost of needing collision handling for back-to-back
// acquisitions (e.g. fast Live view or an MDA) if their interval could
// ever be shorter than the timestamp's resolution.
std::optional<std::string>
UniqueFileName(std::string const &prefix,
               std::vector<std::string> const &extensions);
