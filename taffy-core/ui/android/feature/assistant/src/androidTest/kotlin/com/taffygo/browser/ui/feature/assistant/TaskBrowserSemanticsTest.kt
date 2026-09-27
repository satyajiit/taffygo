// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color as AndroidColor
import android.graphics.Paint
import android.graphics.RectF
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.Density
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.controlTestTag
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import java.io.File

class TaskBrowserSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    @Test
    fun twoPagesAndOneAssistantStayVisibleAndEachCardNamesItsActualTab() {
        val opened = mutableListOf<TabId>()
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = task(),
                    onIntent = {},
                    browserState = pages(),
                    onOpenTab = { opened += it },
                )
            }
        }

        // The request is the screen's title, in the shared bar, rather than an
        // eyebrow over a hero card in the body.
        compose.onNodeWithText("Find this phone for less").assertIsDisplayed()
        captureIfRequested("task-workspace-light.png")
        for (id in listOf("shop-one", "shop-two")) {
            compose.onNodeWithTag("$TASK_BROWSER_TAB_TEST_TAG_PREFIX$id")
                .assertIsDisplayed().assertHasClickAction().performClick()
        }
        compose.onNodeWithTag(TASK_WORKSPACE_ACTIVITY_TEST_TAG).assertIsDisplayed()
        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER)).assertIsDisplayed()
        assertEquals(listOf(TabId("shop-one"), TabId("shop-two")), opened)
    }

    @Test
    fun switchingTasksHidesThePreviousTasksCardsBeforeTheBrowserProjectionCatchesUp() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(
                    state = task().copy(taskId = "next-task"),
                    onIntent = {},
                    browserState = pages(),
                )
            }
        }
        compose.onNodeWithTag(TASK_BROWSER_PANEL_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aHandoverExplainsWhereToFinishTheStepWithoutAskingForCredentialsHere() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                TaskViewContent(
                    state = task().copy(state = TaskDisplayState.WAITING_FOR_YOU, hasHandover = true),
                    onIntent = {},
                    browserState = pages(),
                )
            }
        }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithText(context.getString(R.string.taffy_task_browser_handover))
            .assertIsDisplayed()
        compose.onNodeWithTag(ASK_FIELD_TEST_TAG).assertDoesNotExist()
        captureIfRequested("task-workspace-handover-dark.png")
    }

    @Test
    fun localPageImagesKeepTheirTopEdgeWhenThePortraitSnapshotIsCropped() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaskViewContent(state = task(), onIntent = {}, browserState = pages())
            }
        }
        val bitmap = compose.onNodeWithTag(
            "${TASK_BROWSER_PICTURE_TEST_TAG_PREFIX}shop-one",
            useUnmergedTree = true,
        ).assertIsDisplayed()
            .captureToImage().asAndroidBitmap()
        // The top navigation band survives; a centered crop would discard it.
        assertEquals(PAGE_NAVIGATION_COLOR, bitmap.getPixel(bitmap.width / 2, 5))
        // The lower-page marker starts halfway down the original portrait, outside this crop.
        assertEquals(PAGE_PICTURE_COLOR, bitmap.getPixel(bitmap.width * 9 / 10, bitmap.height - 5))
    }

    @Test
    fun doubledTextStillLeavesEveryPageAndTakeOverReachable() {
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(density.density, fontScale = 2f)) {
                TaffyPreview(darkTheme = false, reducedMotion = true) {
                    TaskViewContent(state = task(), onIntent = {}, browserState = pages())
                }
            }
        }
        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER))
            .assertIsDisplayed().assertHasClickAction()
        // Doubled text does not cost the screen its title: the bar wraps it
        // rather than dropping it, which the hero card could not do.
        compose.onNodeWithText("Find this phone for less").assertExists()
        for (id in listOf("shop-one", "shop-two")) {
            val tag = "$TASK_BROWSER_TAB_TEST_TAG_PREFIX$id"
            compose.onNodeWithTag(TASK_LIST_TEST_TAG).performScrollToNode(hasTestTag(tag))
            compose.onNodeWithTag(tag).assertIsDisplayed().assertHasClickAction()
        }
        captureIfRequested("task-workspace-large-font.png")
        compose.onNodeWithTag(TASK_LIST_TEST_TAG)
            .performScrollToNode(hasTestTag(TASK_WORKSPACE_ACTIVITY_TEST_TAG))
        compose.onNodeWithTag(TASK_WORKSPACE_ACTIVITY_TEST_TAG).assertIsDisplayed()
    }

    private fun task() = TaskViewUiState(
        taskId = "comparison",
        goal = "Find this phone for less",
        state = TaskDisplayState.RUNNING,
        mode = AssistantMode.TAFFY_BROWSES,
        controls = listOf(TaskControl.PAUSE, TaskControl.STOP, TaskControl.TAKE_OVER),
        timeline = listOf(
            TaskTimelineEntry(2, TaskTimelineKind.READ_PAGE, "two.example.test", 2),
            TaskTimelineEntry(1, TaskTimelineKind.READ_PAGE, "one.example.test", 1),
        ),
    )

    private fun pages() = TaskBrowserUiState(
        taskId = "comparison",
        tabs = listOf(
            Tab(TabId("shop-one"), "Phone · 256 GB", "one.example.test", taskId = "comparison"),
            Tab(TabId("shop-two"), "Phone · Compare prices", "two.example.test", taskId = "comparison"),
        ),
        artwork = mapOf(
            TabId("shop-one") to pageArtwork("SHOP ONE", "14,999", AndroidColor.rgb(103, 133, 176)),
            TabId("shop-two") to pageArtwork("SHOP TWO", "15,499", AndroidColor.rgb(159, 112, 140)),
        ),
    )

    /** A synthetic local browser snapshot, deliberately taller than the card's picture frame. */
    private fun pageArtwork(shop: String, price: String, phoneColor: Int): TabArtwork {
        val bitmap = Bitmap.createBitmap(320, 480, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        canvas.drawColor(PAGE_PICTURE_COLOR)
        paint.color = PAGE_NAVIGATION_COLOR
        canvas.drawRect(0f, 0f, 320f, 40f, paint)
        paint.color = AndroidColor.WHITE
        paint.textSize = 15f
        paint.isFakeBoldText = true
        canvas.drawText(shop, 16f, 26f, paint)
        paint.color = AndroidColor.rgb(191, 208, 231)
        canvas.drawOval(RectF(78f, 214f, 254f, 235f), paint)
        paint.color = phoneColor
        canvas.drawRoundRect(RectF(104f, 59f, 201f, 221f), 13f, 13f, paint)
        paint.color = AndroidColor.rgb(20, 26, 36)
        canvas.drawRoundRect(RectF(163f, 71f, 235f, 223f), 12f, 12f, paint)
        paint.color = AndroidColor.rgb(188, 218, 246)
        canvas.drawRoundRect(RectF(168f, 78f, 230f, 216f), 8f, 8f, paint)
        paint.color = AndroidColor.rgb(20, 26, 36)
        canvas.drawCircle(126f, 82f, 10f, paint)
        canvas.drawCircle(151f, 82f, 10f, paint)
        canvas.drawCircle(126f, 107f, 10f, paint)
        paint.color = AndroidColor.rgb(249, 232, 237)
        canvas.drawRect(0f, 240f, 320f, 480f, paint)
        paint.color = PAGE_NAVIGATION_COLOR
        paint.textSize = 20f
        canvas.drawText("Phone / 256 GB", 20f, 284f, paint)
        paint.textSize = 27f
        canvas.drawText(price, 20f, 329f, paint)
        return TabArtwork(thumbnail = bitmap)
    }

    /** Optional visual evidence from the same assertions, outside the shipping source sets. */
    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bitmap = compose.onRoot().captureToImage().asAndroidBitmap()
        File(context.getExternalFilesDir(null), name).outputStream().use {
            check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
        }
    }

    private companion object {
        val PAGE_NAVIGATION_COLOR: Int = AndroidColor.rgb(30, 41, 59)
        val PAGE_PICTURE_COLOR: Int = AndroidColor.rgb(232, 240, 254)
    }
}
