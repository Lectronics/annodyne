// Core contracts of the edit model (ROADMAP Phase 1): effective_state is a pure fold in seq order
// up to the cursor; undo is exact; the history is append-only.

#include "core/edits/effective_state.h"
#include "support/random_scenarios.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

std::vector<EffectiveNote> states(const std::vector<Note>& notes, const EditList& list) {
    std::vector<EffectiveNote> out;
    out.reserve(notes.size());
    for (const Note& n : notes) {
        out.push_back(effective_state(n, list));
    }
    return out;
}

bool near(double a, double b) {
    return std::abs(a - b) <= tolerance::k_fold_commute_abs;
}
bool near_rel(double a, double b) {
    return std::abs(a - b) <= tolerance::k_fold_commute_rel * std::max(1.0, std::abs(a));
}

}  // namespace

TEST_CASE("no_edits_means_effective_state_equals_the_analysed_values", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 5);
        const EditList empty{TrackId{"t1"}};
        for (const Note& n : notes) {
            const EffectiveNote eff = effective_state(n, empty);
            CHECK(eff == neutral_state(n));
            CHECK_FALSE(eff.is_edited);
        }
    }
}

TEST_CASE("add_edits_commute", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 1);
        std::vector<EditSpec> specs;
        double expected_center = notes[0].pitch_center.value();
        double expected_gain = 0.0;
        for (int i = 0, n = uniform_int(rng, 2, 30); i < n; ++i) {
            // Offsets that are not clamped by the fold.
            static constexpr std::array<EditParam, 5> k_params{
                EditParam::center_offset, EditParam::formant_offset, EditParam::gain,
                EditParam::onset_shift, EditParam::noise_gain_sibilant};
            const EditParam p = k_params.at(static_cast<std::size_t>(uniform_int(rng, 0, 4)));
            const ParamInfo& info = param_info(p);
            EditValue v = Decibels{uniform(rng, -12, 12)};
            if (info.kind == ValueKind::cents) {
                v = Cents{uniform(rng, -300, 300)};
            } else if (info.kind == ValueKind::seconds) {
                v = Seconds{uniform(rng, -1, 1)};
            }
            if (p == EditParam::center_offset) {
                expected_center += std::get<Cents>(v).value();
            } else if (p == EditParam::gain) {
                expected_gain += std::get<Decibels>(v).value();
            }
            specs.push_back(make_spec("n0", info.category, p, EditOp::add, v));
        }
        auto fold = [&](const std::vector<EditSpec>& order) {
            EditList list{TrackId{"t1"}};
            for (const auto& s : order) {
                list = list.add(std::vector{s}, k_t0).value();
            }
            return effective_state(notes[0], list);
        };
        const EffectiveNote a = fold(specs);
        // Not just order-independent: the value is pitch_center + sum (a fold that ignored `add`
        // would be order-independent too).
        CHECK(near(a.center.value(), expected_center));
        CHECK(near(a.gain.value(), expected_gain));
        auto shuffled = specs;
        std::ranges::shuffle(shuffled, rng);
        const EffectiveNote b = fold(shuffled);
        CHECK(near(a.center.value(), b.center.value()));
        CHECK(near(a.formant_offset.value(), b.formant_offset.value()));
        CHECK(near(a.gain.value(), b.gain.value()));
        CHECK(near(a.onset_shift.value(), b.onset_shift.value()));
        CHECK(near(noise_gain(a.noise_gain, FrameClass::sibilant).value(),
                   noise_gain(b.noise_gain, FrameClass::sibilant).value()));
    }
}

TEST_CASE("scale_edits_commute", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 1);
        std::vector<EditSpec> specs;
        double expected = 1.0;
        for (int i = 0, n = uniform_int(rng, 2, 12); i < n; ++i) {
            const double f = uniform(rng, 0.25, 4.0);
            expected *= f;
            specs.push_back(make_spec("n0", EditCategory::pitch_mod, EditParam::mod_scale,
                                      EditOp::scale, Ratio{f}));
        }
        auto fold = [&](const std::vector<EditSpec>& order) {
            EditList list{TrackId{"t1"}};
            for (const auto& s : order) {
                list = list.add(std::vector{s}, k_t0).value();
            }
            return effective_state(notes[0], list);
        };
        auto shuffled = specs;
        std::ranges::shuffle(shuffled, rng);
        CHECK(near_rel(fold(specs).mod_scale.value(), fold(shuffled).mod_scale.value()));
        CHECK(near_rel(fold(specs).mod_scale.value(), expected));
    }
}

TEST_CASE("set_overrides_every_earlier_edit_of_its_parameter", "[edit_contract]") {
    for (int seed = 0; seed < 40; ++seed) {
        for (int pi = 0; pi < k_edit_param_count; ++pi) {
            const auto p = static_cast<EditParam>(pi);
            if (p == EditParam::time_handle) {
                continue;  // keyed collection, tested in unit tests
            }
            INFO("seed " << seed << " param " << pi);
            Rng rng{static_cast<std::uint64_t>(seed * 100 + pi)};
            const auto notes = random_notes(rng, 1);
            // random ops on p, then a final set; compare with the final set alone
            auto make = [&](EditOp op) {
                for (;;) {
                    EditSpec s = random_spec(rng, notes);
                    if (s.param == p && s.op == op) {
                        return s;
                    }
                }
            };
            EditList with_history{TrackId{"t1"}};
            for (int i = 0, n = uniform_int(rng, 0, 6); i < n; ++i) {
                const ParamInfo& info = param_info(p);
                EditOp op = EditOp::set;
                if (info.allows_add && uniform_int(rng, 0, 1) != 0) {
                    op = EditOp::add;
                } else if (info.allows_scale) {
                    op = EditOp::scale;
                }
                with_history = with_history.add(std::vector{make(op)}, k_t0).value();
            }
            const EditSpec final_set = make(EditOp::set);
            with_history = with_history.add(std::vector{final_set}, k_t0).value();
            const EditList only = EditList{TrackId{"t1"}}.add(std::vector{final_set}, k_t0).value();
            CHECK(effective_state(notes[0], with_history) == effective_state(notes[0], only));
        }
    }
}

TEST_CASE("undo_restores_the_previous_effective_state_exactly", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 4);
        EditList list{TrackId{"t1"}};
        for (int step = 0; step < 12; ++step) {
            const auto before = states(notes, list);
            const EditList next = list.add(random_group(rng, notes), k_t0).value();
            REQUIRE(states(notes, next.undo().value()) == before);
            list = next;
        }
    }
}

TEST_CASE("any_edit_sequence_followed_by_full_undo_gives_the_original_state", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 4);
        EditList list{TrackId{"t1"}};
        for (int i = 0, n = uniform_int(rng, 1, 25); i < n; ++i) {
            list = list.add(random_group(rng, notes), k_t0).value();
        }
        while (list.can_undo()) {
            list = list.undo().value();
        }
        for (const Note& n : notes) {
            CHECK(effective_state(n, list) == neutral_state(n));
        }
    }
}

TEST_CASE("redo_after_undo_restores_the_state", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 3);
        EditList list{TrackId{"t1"}};
        for (int i = 0, n = uniform_int(rng, 1, 10); i < n; ++i) {
            list = list.add(random_group(rng, notes), k_t0).value();
        }
        const auto full = states(notes, list);
        const int undos = uniform_int(rng, 1, 5);
        EditList walked = list;
        int done = 0;
        for (; done < undos && walked.can_undo(); ++done) {
            walked = walked.undo().value();
        }
        for (int i = 0; i < done; ++i) {
            walked = walked.redo().value();
        }
        CHECK(states(notes, walked) == full);
    }
}

TEST_CASE("a_reset_returns_the_category_to_neutral_and_keeps_history", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 1);
        EditList list{TrackId{"t1"}};
        for (int i = 0, n = uniform_int(rng, 1, 15); i < n; ++i) {
            list = list.add(random_group(rng, notes), k_t0).value();
        }
        const std::size_t before = list.active().size();
        const std::vector<NoteId> ids{notes[0].id};
        std::vector<EditCategory> all;
        for (int c = 0; c <= static_cast<int>(EditCategory::sound); ++c) {
            all.push_back(static_cast<EditCategory>(c));
        }
        list = list.reset(ids, all, k_t0).value();
        CHECK(list.active().size() == before + all.size());
        CHECK(effective_state(notes[0], list) == neutral_state(notes[0]));
    }
}

TEST_CASE("history_is_append_only_and_always_a_valid_list", "[edit_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 3);
        EditList list{TrackId{"t1"}};
        std::vector<std::uint64_t> seen_seqs;  // every seq ever issued, in issue order
        for (int step = 0; step < 30; ++step) {
            const int action = uniform_int(rng, 0, 3);
            if (action == 0 && list.can_undo()) {
                list = list.undo().value();
            } else if (action == 1 && list.can_redo()) {
                list = list.redo().value();
            } else {
                list = list.add(random_group(rng, notes), k_t0).value();
            }
            const auto round = EditList::from_parts(list.to_parts());
            INFO((round.has_value() ? std::string{} : round.error().message));
            REQUIRE(round.has_value());
        }
        // Union of all partitions: strictly increasing in seq after sorting, no duplicates.
        std::vector<std::uint64_t> all;
        for (const auto& e : list.discarded()) {
            all.push_back(e.seq.value);
        }
        for (const auto& e : list.active()) {
            all.push_back(e.seq.value);
        }
        for (const auto& e : list.redo_tail()) {
            all.push_back(e.seq.value);
        }
        std::ranges::sort(all);
        CHECK(std::ranges::adjacent_find(all) == all.end());
        // Nothing is ever deleted: seqs are a gap-free 1..N sequence.
        for (std::size_t i = 0; i < all.size(); ++i) {
            CHECK(all[i] == i + 1);
        }
    }
}
