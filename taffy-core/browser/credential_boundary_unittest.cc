// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/credential_boundary.h"

#include <type_traits>

#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-AUTH-001 and REQ-SEC-001, in two parts.
//
// The first part is a compile-time test and it is the one that matters: on the
// AI side of this component there is no type that can hold a credential value.
// The second part is the run-time behavior — access is suspended while a
// credential interaction is happening, and it resumes exactly when the last
// interaction ends.

namespace taffy {
namespace {

// Detects whether a type has a definition. An incomplete type cannot be
// stored, copied, serialized or inspected, so declaring one and never defining
// it is a way of saying "there is no representation for this" that the
// compiler enforces.
template <typename T, typename = void>
struct IsComplete : std::false_type {};
template <typename T>
struct IsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};

class CredentialBoundaryTest : public testing::Test {
 protected:
  CredentialFieldMetadata PasswordField(const char* tab, const char* node) {
    CredentialFieldMetadata metadata;
    metadata.tab_id = ToRecordIdentifier(tab);
    metadata.frame_id = ToRecordIdentifier("frame_main");
    metadata.node_id = ToRecordIdentifier(node);
    metadata.credential_class = CredentialClass::kPassword;
    metadata.value_is_present = true;
    metadata.filled_by_user = true;
    return metadata;
  }

  content::BrowserTaskEnvironment task_environment_;
  CredentialBoundary boundary_;
};

TEST_F(CredentialBoundaryTest, ACredentialValueHasNoRepresentation) {
  // The boundary type of PAR-AUTH-001. CredentialValue is declared and never
  // defined, so no code in this component can hold one, hand one on, or write
  // one anywhere.
  static_assert(!IsComplete<CredentialValue>::value,
                "CredentialValue must stay an incomplete type. Defining it "
                "would create the first place in the browser process where a "
                "credential value could be stored, which REQ-SEC-001 forbids.");

  // And the metadata that does exist cannot hold one either.
  static_assert(std::is_trivially_copyable_v<CredentialFieldMetadata>);
  static_assert(sizeof(CredentialFieldMetadata) <=
                    3 * (kMaxIdentifierChars + 1) + 8,
                "Credential field metadata grew past the identifiers it is "
                "allowed to carry.");
}

TEST_F(CredentialBoundaryTest, AccessIsSuspendedForTheLifeOfTheInteraction) {
  const TabId tab{"tab_login"};
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));

  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_EQ(AssistantAccess::kSuspendedCredentialInteraction,
            boundary_.EvaluateAssistantAccess(tab));
  EXPECT_FALSE(boundary_.AssistantMayObserve(tab));
  EXPECT_FALSE(boundary_.AssistantMayAct(tab));

  boundary_.EndCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
}

TEST_F(CredentialBoundaryTest, OverlappingInteractionsDoNotEndEachOther) {
  // A password manager filling a password and a one-time code in one gesture
  // is the ordinary case, not the exotic one. If the second end resumed
  // access, the assistant would be watching a tab that is still mid-login.
  const TabId tab{"tab_login"};
  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);
  boundary_.BeginCredentialInteraction(tab, CredentialClass::kOneTimeCode);

  boundary_.EndCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_EQ(AssistantAccess::kSuspendedCredentialInteraction,
            boundary_.EvaluateAssistantAccess(tab));

  boundary_.EndCredentialInteraction(tab, CredentialClass::kOneTimeCode);
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
}

TEST_F(CredentialBoundaryTest, SuspensionIsPerTab) {
  const TabId login{"tab_login"};
  const TabId reading{"tab_reading"};
  boundary_.BeginCredentialInteraction(login, CredentialClass::kPassword);

  EXPECT_FALSE(boundary_.AssistantMayObserve(login));
  // A login in one tab does not stop the browser being useful in another.
  EXPECT_TRUE(boundary_.AssistantMayObserve(reading));
}

TEST_F(CredentialBoundaryTest, HandoffSuspensionIsReportedSeparately) {
  const TabId tab{"tab_oauth"};
  boundary_.SetAuthorizationHandoffPending(tab, true);

  EXPECT_EQ(AssistantAccess::kSuspendedAuthorizationHandoff,
            boundary_.EvaluateAssistantAccess(tab));

  boundary_.SetAuthorizationHandoffPending(tab, false);
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
}

TEST_F(CredentialBoundaryTest, ACredentialInteractionOutranksAHandoff) {
  const TabId tab{"tab_both"};
  boundary_.SetAuthorizationHandoffPending(tab, true);
  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);

  // Both suspend; the stricter and more specific reason is the one reported,
  // because it is the one the user-visible explanation should name.
  EXPECT_EQ(AssistantAccess::kSuspendedCredentialInteraction,
            boundary_.EvaluateAssistantAccess(tab));
}

TEST_F(CredentialBoundaryTest, FieldsAreCountedNotRead) {
  const TabId tab{"tab_login"};
  boundary_.NoteCredentialField(PasswordField("tab_login", "node_password"));
  boundary_.NoteCredentialField(PasswordField("tab_login", "node_confirm"));

  // The count is the entire diagnostic. There is no accessor that could return
  // anything about either field's contents, because nothing recorded any.
  EXPECT_EQ(2u, boundary_.CredentialFieldCount(tab));
  EXPECT_EQ(0u, boundary_.CredentialFieldCount(TabId{"tab_other"}));
}

TEST_F(CredentialBoundaryTest, ACrossDocumentCommitRetiresInteractionState) {
  const TabId tab{"tab_login"};
  boundary_.NoteCredentialField(PasswordField("tab_login", "node_password"));
  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);

  boundary_.ForgetDocument(tab, FrameId{"frame_main"});

  // Interaction state that outlived its document would suspend a tab forever.
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
  EXPECT_EQ(0u, boundary_.CredentialFieldCount(tab));
}

TEST_F(CredentialBoundaryTest, AHandoffSurvivesTheNavigationThatCompletesIt) {
  // An app switch returns through a navigation. Clearing the handoff on a
  // cross-document commit would clear it exactly when the return arrives.
  const TabId tab{"tab_oauth"};
  boundary_.SetAuthorizationHandoffPending(tab, true);

  boundary_.ForgetDocument(tab, FrameId{"frame_main"});

  EXPECT_EQ(AssistantAccess::kSuspendedAuthorizationHandoff,
            boundary_.EvaluateAssistantAccess(tab));
}

TEST_F(CredentialBoundaryTest, ForgettingATabDropsEverythingAboutIt) {
  const TabId tab{"tab_login"};
  boundary_.NoteCredentialField(PasswordField("tab_login", "node_password"));
  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);
  boundary_.SetAuthorizationHandoffPending(tab, true);

  boundary_.ForgetTab(tab);

  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
  EXPECT_EQ(0u, boundary_.CredentialFieldCount(tab));
}

TEST_F(CredentialBoundaryTest, EveryCredentialClassSuspendsAccess) {
  // The exception list is empty today. The predicate exists so a future
  // exception has one place to be argued for; this test is what makes adding
  // one deliberate.
  for (int value = 0;
       value <= static_cast<int>(CredentialClass::kCaptchaAnswer); ++value) {
    EXPECT_TRUE(CredentialClassSuspendsAssistantAccess(
        static_cast<CredentialClass>(value)))
        << "credential class " << value;
  }
}

}  // namespace
}  // namespace taffy
