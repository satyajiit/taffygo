// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/value_reference_vault.h"

#include <string>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// The invariants these tests hold down. Each one is a sentence from
// value_reference_vault.h with a case that makes the refusal fire.
//
//   * a reference is spent by its first successful resolution, and a second
//     resolution of the same reference finds nothing;
//   * a reference belongs to one task and to one profile generation, and it
//     dies with either;
//   * a refused resolution does not spend the reference, because no refusing
//     branch has touched the bytes;
//   * credential material has no clearance, so it cannot be minted for, and a
//     credential target is refused ahead of every other reason;
//   * a reference is not a function of the value: two mints of identical bytes
//     produce different references, and a one-character value and a
//     four-thousand-character value produce references of the same length.

namespace taffy {
namespace {

class ValueReferenceVaultTest : public testing::Test {
 protected:
  void SetUp() override { vault_.BeginGeneration("profile_1", 1u); }

  base::TimeTicks Now() const { return task_environment_.NowTicks(); }
  base::TimeTicks Soon() const { return Now() + base::Seconds(30); }

  // Every mint in this file goes through here, so that the clearance is
  // constructed the one way it can be.
  ValueReference MintIdentity(const TaskId& task_id, std::string value) {
    const std::optional<FillClearance> clearance =
        FillClearance::For(Sensitivity::kIdentity);
    CHECK(clearance.has_value());
    return vault_.Mint(task_id, *clearance, std::move(value), Soon());
  }

  // The named twin, for the crossing decision 0088 describes: the browser
  // mints under a name it derived, and the isolated core later composes a
  // proposal naming the same one without either side ever having sent it.
  ValueReference MintIdentityNamed(const TaskId& task_id,
                                   const std::string& name,
                                   std::string value) {
    const std::optional<FillClearance> clearance =
        FillClearance::For(Sensitivity::kIdentity);
    CHECK(clearance.has_value());
    return vault_.MintNamed(ValueReference{name}, task_id, *clearance,
                            std::move(value), Soon());
  }

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ValueReferenceVault vault_;
  const TaskId task_{"task_1"};
};

// --- the clearance, which is the credential invariant ------------------------

TEST_F(ValueReferenceVaultTest, NoClearanceExistsForAnyCredentialClass) {
  // The compile-time half of this is the static_assert in the header. This is
  // the same statement at run time, plus every other class the fill table
  // calls never fillable, so that a class quietly acquiring a clearance fails
  // here as well as there.
  EXPECT_FALSE(FillClearance::For(Sensitivity::kCredential).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kUnknownSensitive).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kPayment).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kHealth).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kFinancial).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kLegal).has_value());
  EXPECT_FALSE(
      FillClearance::For(Sensitivity::kPrivateCommunication).has_value());
  EXPECT_FALSE(FillClearance::For(Sensitivity::kAdministration).has_value());

  // And the four that do have one, so that this test cannot pass by refusing
  // everything.
  EXPECT_TRUE(FillClearance::For(Sensitivity::kNotSensitive).has_value());
  EXPECT_TRUE(FillClearance::For(Sensitivity::kPersonal).has_value());
  EXPECT_TRUE(FillClearance::For(Sensitivity::kAccount).has_value());
  EXPECT_TRUE(FillClearance::For(Sensitivity::kIdentity).has_value());
  EXPECT_TRUE(FillClearance::For(Sensitivity::kOneTimeCode).has_value());
  EXPECT_TRUE(FillClearance::For(Sensitivity::kChallengeResponse).has_value());
}

TEST_F(ValueReferenceVaultTest, AClearanceRemembersOnlyTheFieldClass) {
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kPersonal);
  ASSERT_TRUE(clearance.has_value());
  EXPECT_EQ(Sensitivity::kPersonal, clearance->field_class());
}

TEST_F(ValueReferenceVaultTest, ACredentialFieldIsRefusedAtResolution) {
  // A perfectly good reference, aimed at a password box. The refusal is not
  // about the reference at all: nothing Taffy holds may enter a credential
  // field, whoever minted it and whatever it is for.
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  ASSERT_TRUE(reference.is_valid());

  std::string out = "untouched";
  EXPECT_EQ(
      ValueResolution::kFieldMayNotBeFilled,
      vault_.Resolve(reference, task_, Sensitivity::kCredential, Now(), out));
  EXPECT_EQ("untouched", out);
  // Refusing did not spend it. The person's value is still theirs.
  EXPECT_TRUE(vault_.Holds(reference));
}

TEST_F(ValueReferenceVaultTest, ThePermanentRefusalIsReportedFirst) {
  // Expired *and* aimed at a credential field. The answer names the reason
  // that will never change, so the sentence a person reads about a credential
  // field does not appear to soften when the other condition is fixed.
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  task_environment_.FastForwardBy(base::Minutes(5));

  std::string out;
  EXPECT_EQ(
      ValueResolution::kFieldMayNotBeFilled,
      vault_.Resolve(reference, task_, Sensitivity::kCredential, Now(), out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ValueReferenceVaultTest, EveryNeverFillableTargetIsRefused) {
  std::string out;
  for (const Sensitivity target :
       {Sensitivity::kCredential, Sensitivity::kUnknownSensitive,
        Sensitivity::kPayment, Sensitivity::kHealth, Sensitivity::kFinancial,
        Sensitivity::kLegal, Sensitivity::kPrivateCommunication,
        Sensitivity::kAdministration}) {
    const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
    EXPECT_EQ(ValueResolution::kFieldMayNotBeFilled,
              vault_.Resolve(reference, task_, target, Now(), out));
    EXPECT_TRUE(out.empty());
  }
}

// --- one use -----------------------------------------------------------------

TEST_F(ValueReferenceVaultTest, AReferenceIsSpentByItsFirstResolution) {
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  ASSERT_TRUE(reference.is_valid());
  ASSERT_EQ(1u, vault_.HeldCount());

  std::string out;
  ASSERT_EQ(
      ValueResolution::kResolved,
      vault_.Resolve(reference, task_, Sensitivity::kIdentity, Now(), out));
  EXPECT_EQ("1234 5678 9012", out);
  EXPECT_EQ(0u, vault_.HeldCount());
  EXPECT_FALSE(vault_.Holds(reference));

  // A reference that could be presented twice would be a value with extra
  // steps. The second presentation finds nothing, and it cannot tell that
  // there ever was anything.
  std::string second = "untouched";
  EXPECT_EQ(
      ValueResolution::kUnknownReference,
      vault_.Resolve(reference, task_, Sensitivity::kIdentity, Now(), second));
  EXPECT_EQ("untouched", second);
}

TEST_F(ValueReferenceVaultTest, AReferenceNobodyMintedIsRefused) {
  std::string out;
  EXPECT_EQ(ValueResolution::kUnknownReference,
            vault_.Resolve(ValueReference{"val_invented_by_the_model"}, task_,
                           Sensitivity::kIdentity, Now(), out));
  EXPECT_TRUE(out.empty());
}

// --- the task and generation binding -----------------------------------------

TEST_F(ValueReferenceVaultTest, AReferenceBelongsToOneTask) {
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");

  std::string out;
  EXPECT_EQ(ValueResolution::kNotThisTask,
            vault_.Resolve(reference, TaskId{"task_2"}, Sensitivity::kIdentity,
                           Now(), out));
  EXPECT_TRUE(out.empty());
  // The other task's attempt did not cost this task its value.
  EXPECT_TRUE(vault_.Holds(reference));
  EXPECT_EQ(
      ValueResolution::kResolved,
      vault_.Resolve(reference, task_, Sensitivity::kIdentity, Now(), out));
}

TEST_F(ValueReferenceVaultTest, EndingATaskDropsWhatItHeld) {
  const ValueReference mine = MintIdentity(task_, "1234 5678 9012");
  const ValueReference theirs = MintIdentity(TaskId{"task_2"}, "9876 5432");

  vault_.RevokeTask(task_);
  EXPECT_FALSE(vault_.Holds(mine));
  EXPECT_TRUE(vault_.Holds(theirs));
  EXPECT_EQ(1u, vault_.HeldCount());
}

TEST_F(ValueReferenceVaultTest, RevokingOneReferenceLeavesItsSiblingHeld) {
  const ValueReference first = MintIdentity(task_, "1234 5678 9012");
  const ValueReference second = MintIdentity(task_, "9876 5432 1098");
  ASSERT_EQ(2u, vault_.HeldCount());

  vault_.RevokeReference(first);
  EXPECT_FALSE(vault_.Holds(first));
  EXPECT_TRUE(vault_.Holds(second));
  EXPECT_EQ(1u, vault_.HeldCount());

  // A repeat and an unknown name reveal nothing and disturb nothing.
  vault_.RevokeReference(first);
  vault_.RevokeReference(ValueReference{"val_never_minted"});
  EXPECT_TRUE(vault_.Holds(second));
}

TEST_F(ValueReferenceVaultTest, ANewGenerationInheritsNothing) {
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  ASSERT_TRUE(vault_.Holds(reference));

  vault_.BeginGeneration("profile_1", 2u);
  EXPECT_EQ(0u, vault_.HeldCount());
  EXPECT_FALSE(vault_.Holds(reference));
}

TEST_F(ValueReferenceVaultTest, RevokingTheGenerationDropsEverything) {
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  vault_.RevokeGeneration(1u);

  std::string out;
  EXPECT_EQ(
      ValueResolution::kUnknownReference,
      vault_.Resolve(reference, task_, Sensitivity::kIdentity, Now(), out));
  EXPECT_EQ(0u, vault_.HeldCount());
}

// --- lifetime ----------------------------------------------------------------

TEST_F(ValueReferenceVaultTest, AnExpiredReferenceIsRefusedAndDropped) {
  // A resolution that arrives after the deadline but before the timer's own
  // pass. The clock the vault runs its timer on is not advanced here; what is
  // advanced is the caller's reading of it, which is the argument Resolve()
  // actually decides on. That ordering is real rather than contrived - a
  // dispatch reads the clock, does some work and then resolves - and it is the
  // only way the kExpired verdict is reached now that a deadline is kept by
  // the vault rather than by whoever happens to ask next.
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  ASSERT_EQ(1u, vault_.HeldCount());

  std::string out;
  EXPECT_EQ(ValueResolution::kExpired,
            vault_.Resolve(reference, task_, Sensitivity::kIdentity,
                           Now() + base::Minutes(5), out));
  EXPECT_TRUE(out.empty());
  // Pruned rather than spent: it can never produce bytes again, so leaving it
  // in the map would only keep the value in memory for longer.
  EXPECT_EQ(0u, vault_.HeldCount());
}

TEST_F(ValueReferenceVaultTest, AnExpiredValueIsDroppedWithoutBeingAskedFor) {
  // The case the deadline exists for, and the one nobody asks about. A
  // reference the assistant never proposes an action for is the *likely*
  // outcome, not the exceptional one: a person types their identifier, the
  // errand goes another way, and nothing ever resolves it. If the record is
  // pruned only by a resolution that never comes, "held for seconds rather
  // than for a session" is a sentence about the deadline rather than about the
  // value, and the bytes sit in the browser process until the profile
  // generation ends.
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");
  ASSERT_EQ(1u, vault_.HeldCount());

  task_environment_.FastForwardBy(base::Minutes(5));

  EXPECT_EQ(0u, vault_.HeldCount())
      << "an expired value must be dropped by its own deadline, not by the "
         "next caller who happens to name it";
  EXPECT_FALSE(vault_.Holds(reference));
}

TEST_F(ValueReferenceVaultTest, EachDeadlineIsKeptAndTheNextOneIsRearmed) {
  // Two values with different deadlines, dropped one at a time. One timer
  // serves the whole map, so the pass that the earlier deadline wakes has to
  // leave the later one still held *and* still scheduled - a sweep that
  // forgot to re-arm would keep the first value's promise and quietly break
  // the second's, which is the failure that looks like success.
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kIdentity);
  ASSERT_TRUE(clearance.has_value());
  const ValueReference early = vault_.Mint(task_, *clearance, "1234 5678 9012",
                                           Now() + base::Minutes(1));
  const ValueReference late = vault_.Mint(task_, *clearance, "9876 5432 1098",
                                          Now() + base::Minutes(3));
  ASSERT_EQ(2u, vault_.HeldCount());

  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_FALSE(vault_.Holds(early));
  EXPECT_TRUE(vault_.Holds(late));

  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_FALSE(vault_.Holds(late));
  EXPECT_EQ(0u, vault_.HeldCount());
}

// --- the class the value was given for ---------------------------------------

TEST_F(ValueReferenceVaultTest, AValueDoesNotMoveToAFieldOfAnotherClass) {
  const ValueReference reference = MintIdentity(task_, "1234 5678 9012");

  std::string out;
  EXPECT_EQ(
      ValueResolution::kFieldClassMismatch,
      vault_.Resolve(reference, task_, Sensitivity::kPersonal, Now(), out));
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(vault_.Holds(reference));
}

// --- what a reference discloses ----------------------------------------------

TEST_F(ValueReferenceVaultTest, AReferenceIsNotAFunctionOfTheValue) {
  // Two mints of identical bytes. A reference derived from the value - a
  // digest, a keyed hash, anything - would make these equal, and equality
  // would let a holder of two references learn that two fields carry the same
  // value without holding either.
  const ValueReference first = MintIdentity(task_, "1234 5678 9012");
  const ValueReference second = MintIdentity(task_, "1234 5678 9012");
  EXPECT_NE(first.value, second.value);

  // And the reference says nothing about the size of what it names. A
  // one-character value and a four-thousand-character one produce references
  // of exactly the same length, so a reference in a journal record or a model
  // message discloses no more than that a value exists.
  const ValueReference tiny = MintIdentity(task_, "1");
  const ValueReference huge = MintIdentity(task_, std::string(4000u, 'x'));
  EXPECT_EQ(tiny.value.size(), huge.value.size());

  // Nothing of the value appears in its own reference.
  EXPECT_EQ(std::string::npos, first.value.find("1234"));
  EXPECT_TRUE(first.is_valid());
}

// --- a name both sides derived ----------------------------------------------

TEST_F(ValueReferenceVaultTest, ADerivedNameMakesTheRoundTrip) {
  // The whole of what makes decision 0088's count-only crossing sufficient.
  // The browser mints under `{request_id}-value-{index}`; the isolated core
  // later names the same string, computed from the same two facts, with
  // nothing having been sent between them. If the two derivations ever
  // disagree by a character, this is what stops passing.
  const std::string name = "turn-4-values-1-value-0";
  const ValueReference minted =
      MintIdentityNamed(task_, name, "1234 5678 9012");
  ASSERT_TRUE(minted.is_valid());
  EXPECT_EQ(name, minted.value);
  EXPECT_TRUE(vault_.Holds(ValueReference{name}));

  // Named independently, exactly as the other side would.
  std::string out;
  EXPECT_EQ(ValueResolution::kResolved,
            vault_.Resolve(ValueReference{name}, task_, Sensitivity::kIdentity,
                           Now(), out));
  EXPECT_EQ("1234 5678 9012", out);
  // And spent by that one resolution, like any other reference: a derived
  // name is a name, not a licence to present the value twice.
  EXPECT_FALSE(vault_.Holds(ValueReference{name}));
}

TEST_F(ValueReferenceVaultTest, ASecondValueUnderOneNameIsRefused) {
  // The vault's half of "a request is answered once". The second mint finds
  // the first one's record already there and is refused rather than replacing
  // it — replacing would destroy a value the core may already have been told
  // about, and destroy it silently.
  ASSERT_TRUE(
      MintIdentityNamed(task_, "turn-4-values-1-value-0", "first").is_valid());
  EXPECT_FALSE(
      MintIdentityNamed(task_, "turn-4-values-1-value-0", "second").is_valid());
  EXPECT_EQ(1u, vault_.HeldCount());

  std::string out;
  EXPECT_EQ(ValueResolution::kResolved,
            vault_.Resolve(ValueReference{"turn-4-values-1-value-0"}, task_,
                           Sensitivity::kIdentity, Now(), out));
  EXPECT_EQ("first", out);
}

TEST_F(ValueReferenceVaultTest, ANameThatIsNotOneHoldsNothing) {
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kIdentity);
  ASSERT_TRUE(clearance.has_value());

  // Empty, and longer than an identifier may be. Both refuse before anything
  // is held, so "the browser holds a value" and "the browser has a name for
  // it" stay the same statement.
  EXPECT_FALSE(
      vault_.MintNamed(ValueReference{}, task_, *clearance, "value", Soon())
          .is_valid());
  const std::string overlong(kMaxIdentifierChars + 1u, 'n');
  EXPECT_FALSE(vault_
                   .MintNamed(ValueReference{overlong}, task_, *clearance,
                              "value", Soon())
                   .is_valid());
  EXPECT_EQ(0u, vault_.HeldCount());
}

TEST_F(ValueReferenceVaultTest, ADerivedNameIsBoundLikeEveryOtherReference) {
  const std::string name = "turn-4-values-1-value-0";
  ASSERT_TRUE(MintIdentityNamed(task_, name, "1234 5678 9012").is_valid());

  // Another task's, refused. A person's answer to one errand is not available
  // to the next one, and deriving the name changes nothing about that.
  std::string out;
  EXPECT_EQ(ValueResolution::kNotThisTask,
            vault_.Resolve(ValueReference{name}, TaskId{"task_2"},
                           Sensitivity::kIdentity, Now(), out));
  // A credential target, refused ahead of everything else.
  EXPECT_EQ(ValueResolution::kFieldMayNotBeFilled,
            vault_.Resolve(ValueReference{name}, task_,
                           Sensitivity::kCredential, Now(), out));
  // A field of another class, refused.
  EXPECT_EQ(ValueResolution::kFieldClassMismatch,
            vault_.Resolve(ValueReference{name}, task_, Sensitivity::kPersonal,
                           Now(), out));
  EXPECT_TRUE(out.empty());
  // None of those spent it.
  EXPECT_TRUE(vault_.Holds(ValueReference{name}));
}

// --- what cannot be minted ---------------------------------------------------

TEST_F(ValueReferenceVaultTest, MintRefusesWhatItCannotHoldMeaningfully) {
  const std::optional<FillClearance> clearance =
      FillClearance::For(Sensitivity::kPersonal);
  ASSERT_TRUE(clearance.has_value());

  // An empty value. "The person entered nothing" is not something to write
  // into a field later, and a reference resolving to nothing would make "the
  // browser holds a value" and "the browser has a name for one" different
  // statements.
  EXPECT_FALSE(
      vault_.Mint(task_, *clearance, std::string(), Soon()).is_valid());
  // A task identifier that is not one.
  EXPECT_FALSE(vault_.Mint(TaskId{}, *clearance, "value", Soon()).is_valid());
  EXPECT_EQ(0u, vault_.HeldCount());

  // And nothing at all before a profile generation is active, because a value
  // with no generation belongs to no profile.
  ValueReferenceVault fresh;
  EXPECT_FALSE(fresh.Mint(task_, *clearance, "value", Soon()).is_valid());
  EXPECT_EQ(0u, fresh.HeldCount());
}

}  // namespace
}  // namespace taffy
