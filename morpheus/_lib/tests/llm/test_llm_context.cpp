/*
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../test_utils/common.hpp"  // IWYU pragma: associated

#include "morpheus/llm/input_map.hpp"
#include "morpheus/llm/llm_context.hpp"
#include "morpheus/llm/llm_lambda_node.hpp"
#include "morpheus/llm/llm_node.hpp"
#include "morpheus/llm/llm_node_runner.hpp"
#include "morpheus/llm/llm_task.hpp"
#include "morpheus/types.hpp"

#include <gtest/gtest.h>
#include <mrc/channel/forward.hpp>
#include <mrc/coroutines/sync_wait.hpp>
#include <nlohmann/json.hpp>

#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>

using namespace morpheus;
using namespace morpheus::test;
using namespace mrc;

TEST_CLASS(LLMContext);

TEST_F(TestLLMContext, Initialization)
{
    llm::LLMContext ctx_1;

    nlohmann::json task_dict;
    task_dict = {
        {"task_type", "dictionary"},
        {"model_name", "test"},
    };

    llm::LLMContext ctx_2{llm::LLMTask{"template", task_dict}, nullptr};
    ASSERT_EQ(ctx_2.task().get("task_type"), "dictionary");
    ASSERT_EQ(ctx_2.task().get("model_name"), "test");

    nlohmann::json msg_config;
    msg_config["tasks"] = {{{"type", "llm_engine"}, {"properties", {{"type", "template"}, {"properties", task_dict}}}}};

    auto msg = std::make_shared<ControlMessage>(msg_config);

    llm::LLMContext ctx_3{llm::LLMTask{}, msg};
    ASSERT_EQ(ctx_3.message()->has_task("llm_engine"), true);

    llm::LLMContext ctx_4{llm::LLMTask{"template", task_dict}, msg};
    ASSERT_EQ(ctx_4.message()->has_task("llm_engine"), true);
    ASSERT_EQ(ctx_4.task().get("task_type"), "dictionary");
    ASSERT_EQ(ctx_4.task().get("model_name"), "test");

    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{"template", task_dict}, msg);
    auto inputs     = llm::input_mappings_t{{"/ext1", "input1"}};
    llm::LLMContext ctx_5{parent_ctx, "child", inputs};
    ASSERT_EQ(ctx_5.input_map()[0].external_name, "/ext1");
    ASSERT_EQ(ctx_5.input_map()[0].internal_name, "input1");
    ASSERT_EQ(ctx_5.parent()->message()->has_task("llm_engine"), true);
    ASSERT_EQ(ctx_5.parent()->task().get("task_type"), "dictionary");
    ASSERT_EQ(ctx_5.parent()->task().get("model_name"), "test");
    ASSERT_EQ(ctx_5.name(), "child");
    ASSERT_EQ(ctx_5.full_name(), "/child");
}

TEST_F(TestLLMContext, SetOutput)
{
    llm::LLMContext ctx_1{llm::LLMTask{}, nullptr};
    nlohmann::json outputs;
    outputs = {{"key1", "val1"}, {"key2", "val2"}};
    ctx_1.set_output(outputs);

    ASSERT_EQ(ctx_1.all_outputs()["key1"], "val1");
    ASSERT_EQ(ctx_1.all_outputs()["key2"], "val2");

    llm::LLMContext ctx_2{llm::LLMTask{}, nullptr};
    ctx_2.set_output("output", outputs);
    ASSERT_EQ(ctx_2.all_outputs()["output"]["key1"], "val1");
    ASSERT_EQ(ctx_2.all_outputs()["output"]["key2"], "val2");
}

TEST_F(TestLLMContext, PushPop)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    auto inputs     = llm::input_mappings_t{{"/ext1", "input1"}};
    auto child_ctx  = parent_ctx->push("child", inputs);

    ASSERT_EQ(child_ctx->name(), "child");
    ASSERT_EQ(child_ctx->input_map()[0].external_name, "/ext1");
    ASSERT_EQ(child_ctx->input_map()[0].internal_name, "input1");

    nlohmann::json outputs;
    outputs = {{"key1", "val1"}, {"key2", "val2"}};
    child_ctx->set_output(outputs);
    ASSERT_EQ(child_ctx->all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx->all_outputs()["key2"], "val2");

    child_ctx->pop();
    ASSERT_EQ(child_ctx->all_outputs(), nullptr);
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"]["key1"], "val1");
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"]["key2"], "val2");
}

TEST_F(TestLLMContext, PopWithoutPush)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    auto inputs     = llm::input_mappings_t{{"/ext1", "input1"}};
    llm::LLMContext child_ctx{parent_ctx, "child", inputs};

    ASSERT_EQ(child_ctx.name(), "child");
    ASSERT_EQ(child_ctx.input_map()[0].external_name, "/ext1");
    ASSERT_EQ(child_ctx.input_map()[0].internal_name, "input1");

    nlohmann::json outputs;
    outputs = {{"key1", "val1"}, {"key2", "val2"}};
    child_ctx.set_output(outputs);
    ASSERT_EQ(child_ctx.all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx.all_outputs()["key2"], "val2");

    child_ctx.pop();
    ASSERT_EQ(child_ctx.all_outputs(), nullptr);
    ASSERT_EQ(parent_ctx->all_outputs()["child"]["key1"], "val1");
    ASSERT_EQ(parent_ctx->all_outputs()["child"]["key2"], "val2");
}

TEST_F(TestLLMContext, PopSelectOneOutput)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    auto inputs     = llm::input_mappings_t{{"/ext1", "input1"}};
    auto child_ctx  = parent_ctx->push("child", inputs);

    nlohmann::json outputs;
    outputs = {{"key1", "val1"}, {"key2", "val2"}, {"key3", "val3"}};
    child_ctx->set_output(outputs);
    ASSERT_EQ(child_ctx->all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx->all_outputs()["key2"], "val2");
    ASSERT_EQ(child_ctx->all_outputs()["key3"], "val3");

    child_ctx->set_output_names({"key2"});
    child_ctx->pop();
    // std::cerr << child_ctx->all_outputs();
    ASSERT_EQ(child_ctx->all_outputs().size(), 3);
    ASSERT_EQ(child_ctx->all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx->all_outputs()["key2"], nullptr);
    ASSERT_EQ(child_ctx->all_outputs()["key3"], "val3");
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"].size(), 1);
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"], "val2");
}

TEST_F(TestLLMContext, PopSelectMultipleOutputs)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    auto inputs     = llm::input_mappings_t{{"/ext1", "input1"}};
    auto child_ctx  = parent_ctx->push("child", inputs);

    nlohmann::json outputs;
    outputs = {{"key1", "val1"}, {"key2", "val2"}, {"key3", "val3"}};
    child_ctx->set_output(outputs);
    ASSERT_EQ(child_ctx->all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx->all_outputs()["key2"], "val2");
    ASSERT_EQ(child_ctx->all_outputs()["key3"], "val3");

    child_ctx->set_output_names({"key2", "key3"});
    child_ctx->pop();
    ASSERT_EQ(child_ctx->all_outputs().size(), 3);
    ASSERT_EQ(child_ctx->all_outputs()["key1"], "val1");
    ASSERT_EQ(child_ctx->all_outputs()["key2"], "val2");
    ASSERT_EQ(child_ctx->all_outputs()["key3"], "val3");
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"].size(), 2);
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"]["key2"], "val2");
    ASSERT_EQ(child_ctx->parent()->all_outputs()["child"]["key3"], "val3");
}

TEST_F(TestLLMContext, SingleInputMapping)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    nlohmann::json outputs;
    outputs = {{"parent_out", "val1"}};
    parent_ctx->set_output(outputs);

    auto inputs_1 = llm::input_mappings_t{{"/parent_out", "input1"}};
    llm::LLMContext child_ctx_1{parent_ctx, "child", inputs_1};
    ASSERT_EQ(child_ctx_1.get_input(), "val1");
    ASSERT_EQ(child_ctx_1.get_input("input1"), "val1");
    ASSERT_EQ(child_ctx_1.get_inputs()["input1"], "val1");
    ASSERT_THROW(child_ctx_1.get_input("input2"), std::runtime_error);

    auto inputs_2 = llm::input_mappings_t{{"/invalid", "input1"}};
    llm::LLMContext child_ctx_2{parent_ctx, "child", inputs_2};
    ASSERT_THROW(child_ctx_2.get_input(), std::runtime_error);
    ASSERT_THROW(child_ctx_2.get_input("input1"), std::runtime_error);
    ASSERT_THROW(child_ctx_2.get_inputs()["input1"], std::runtime_error);
}

TEST_F(TestLLMContext, MultipleInputMappings)
{
    auto parent_ctx = std::make_shared<llm::LLMContext>(llm::LLMTask{}, nullptr);
    nlohmann::json outputs;
    outputs = {{"parent_out1", "val1"}, {"parent_out2", "val2"}};
    parent_ctx->set_output(outputs);

    auto inputs_1 = llm::input_mappings_t{{"/parent_out1", "input1"}, {"/parent_out2", "input2"}};
    llm::LLMContext child_ctx_1{parent_ctx, "child", inputs_1};
    ASSERT_EQ(child_ctx_1.get_input("input1"), "val1");
    ASSERT_EQ(child_ctx_1.get_input("input2"), "val2");
    ASSERT_EQ(child_ctx_1.get_inputs()["input1"], "val1");
    ASSERT_EQ(child_ctx_1.get_inputs()["input2"], "val2");
    ASSERT_THROW(child_ctx_1.get_input(), std::runtime_error);
    ASSERT_THROW(child_ctx_1.get_input("input3"), std::runtime_error);

    auto inputs_2 = llm::input_mappings_t{{"/invalid", "input1"}};
    llm::LLMContext child_ctx_2{parent_ctx, "child", inputs_2};
    ASSERT_THROW(child_ctx_2.get_input(), std::runtime_error);
    ASSERT_THROW(child_ctx_2.get_input("input1"), std::runtime_error);
    ASSERT_THROW(child_ctx_2.get_inputs()["input1"], std::runtime_error);

    auto inputs_3 = llm::input_mappings_t{{"/parent_out1", "input1"}, {"/invalid", "input2"}};
    llm::LLMContext child_ctx_3{parent_ctx, "child", inputs_2};
    ASSERT_EQ(child_ctx_1.get_input("input1"), "val1");
    ASSERT_THROW(child_ctx_2.get_input("input2"), std::runtime_error);
}
