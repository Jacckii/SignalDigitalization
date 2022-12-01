#include <exprtk/exprtk.hpp>
#include "plot_manager.h"
#include <optional>
#include <MagicEnum/magic_enum.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <implot.h>
#include <random>
#include <implot_internal.h>

PlotManager plot_manager;

typedef exprtk::symbol_table<float> symbol_table_t;
typedef exprtk::expression<float> expression_t;
typedef exprtk::parser<float>         parser_t;

PlotManager::PlotManager()
{
    constexpr auto enum_data = magic_enum::enum_values<PlotManager::function_names>();
    for (auto& iter : enum_data) {
        vec_function_names.push_back(std::string(magic_enum::enum_name(iter)));
    }
}

void PlotManager::TickData()
{
    if (paused)
        return;

    time += ImGui::GetIO().DeltaTime;

    for (auto& iter : inputs) {
        TickInputData(iter);
        TickOutputData(iter);
    }
}

void PlotManager::OpenAddInputDialog()
{
	ImGui::OpenPopup("Add input");
}

void PlotManager::RenderAddInputDialog()
{
    if (ImGui::BeginPopupModal("Add input", NULL))
    {
        static int uid = 0;
        static char name[256] = "Analog_0\0";
        static char math_expression[500] = "\0";
        static int item = 0;
        static float amplitude = 1.f;
        ImGui::Text("Select input type:");
        ImGui::ComboV("##input_function", &item, vec_function_names);
        ImGui::Text("Input name:");
        ImGui::InputText("##InputNameInput", name, 256);
        static float color[4] = { 0.4f, 0.7f, 0.0f, 1.f };
        ImGui::Text("Plot color:");
        ImGui::ColorEdit4("##PlotColor", color);

        if (item == PlotManager::function_names::math_expression) {
            ImGui::Text("Math expression:");
            ImGui::InputText("##MathExpressionInput", math_expression, 500);
        }
        else {
            ImGui::Text("Amplitude:");
            ImGui::SliderFloat("##AmplitudeSlider", &amplitude, 0.01f, 1.f, "%.2f");
        }

        ImGui::Dummy(ImVec2(300.f, 15.f));

        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.98f, 0.48f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.98f, 0.48f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.41f, 0.99f, 0.57f, 0.7f));
        if (ImGui::Button("Add"))
        {
            auto new_name = std::string(name);
            if (CheckIfInputNameExists(new_name))
                new_name += std::string("#") + std::to_string(uid);

            PlotManager::Signal temp(
                std::string(new_name),
                (PlotManager::function_names)item, 
                std::string(math_expression), 
                ImVec4(color[0], color[1], color[2], color[3]),
                amplitude);

            inputs.push_back(temp);
            uid++;
            sprintf(name, "Analog_%d\0", uid);

            //generate new color using colormaps
            ImPlot::BeginPlot("##DUMMY");
            auto new_color = ImPlot::NextColormapColor();
            ImPlot::EndPlot();

            color[0] = new_color.x;
            color[1] = new_color.y;
            color[2] = new_color.z;
            color[3] = new_color.w;

            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
}

void PlotManager::RenderInputCards()
{
    for (size_t i = 0; i < inputs.size(); i++) {
        auto& iter = inputs[i];
        auto ret = RenderAnalogInputCard(iter);
        if (ret == 0)
            continue;

        //edit
        if (ret == 1)
        {
            edit_input = (int)i;
            OpenEditInputDialog();
        }

        if (ret == 2) {
            edit_input = -1;
            DeleteInput(i);
            break;
        }
    }
    RenderEditInputDialog();
}

void PlotManager::RenderMainPlot()
{
    auto old_plot_style = ImPlot::GetStyle();
    ImPlot::GetStyle().MarkerSize = marker_size;

    auto plot_window_height = ImGui::GetCurrentWindow()->Size.y - (ImGui::GetStyle().ItemSpacing.y * 4.f);
    ImGui::BeginChild("Plot view", ImVec2(0.f, (plot_window_height * 0.65f) - (ImGui::GetStyle().ItemSpacing.y * 1.25f)), true);
    if (ImPlot::BeginPlot("##Digital", ImVec2(-1.f, -1.f), auto_size ? ImPlotFlags_Equal : 0)) {
        ImPlot::SetupAxes(0, 0, 0, auto_size ? ImPlotAxisFlags_AutoFit : 0);
        ImPlot::SetupAxisLimits(ImAxis_X1, time - 10.0, time, ImGuiCond_Always);
        for (auto& iter : inputs) {
            if (iter.data_buffer_analog.Data.size() > 0)
            {
                ImPlot::PushStyleColor(ImPlotCol_Line, iter.plot_color);
                ImPlot::PlotLine(iter.input_name.c_str(), &iter.data_buffer_analog.Data[0].x, &iter.data_buffer_analog.Data[0].y, iter.data_buffer_analog.Data.size(), iter.data_buffer_analog.Offset, 2 * sizeof(float));
                ImPlot::PopStyleColor();
                if (show_sampling && iter.data_buffer_sampling.Data.size() > 0) {
                    if (sample_show_type == 0) {
                        ImPlot::PlotScatter((iter.input_name + std::string(" sample points")).c_str(), &iter.data_buffer_sampling.Data[0].x, &iter.data_buffer_sampling.Data[0].y, iter.data_buffer_sampling.Data.size(), iter.data_buffer_sampling.Offset, 2 * sizeof(float));
                    }
                    else if (sample_show_type == 1)
                    {
                        ImPlot::PlotStems((iter.input_name + std::string(" sample points")).c_str(), &iter.data_buffer_sampling.Data[0].x, &iter.data_buffer_sampling.Data[0].y, iter.data_buffer_sampling.Data.size(), 0, iter.data_buffer_sampling.Offset, 2 * sizeof(float));
                    }
                }

                if (show_quant_data && iter.data_buffer_quantization.Data.size() > 0) {
                    if (quant_show_type == 0) {
                        ImPlot::PlotStairs((iter.input_name + std::string(" quantizied")).c_str(), &iter.data_buffer_quantization.Data[0].x, &iter.data_buffer_quantization.Data[0].y, iter.data_buffer_quantization.Data.size(), iter.data_buffer_quantization.Offset, 2 * sizeof(float));
                    }
                    else if (quant_show_type == 1) {
                        ImPlot::PlotLine((iter.input_name + std::string(" quantizied")).c_str(), &iter.data_buffer_quantization.Data[0].x, &iter.data_buffer_quantization.Data[0].y, iter.data_buffer_quantization.Data.size(), iter.data_buffer_quantization.Offset, 2 * sizeof(float));
                    }
                }

                if (show_digital_data && iter.data_buffer_quantization_index.Data.size() > 0 && iter.data_buffer_quantization.Data.size() > 0) {
                    //render text
                    auto last_index_value = ImVec2(0,0);
                    if (iter.data_buffer_quantization_index.Offset == 0)
                        last_index_value = iter.data_buffer_quantization_index.Data.back();
                    else {
                        auto data_index = (iter.data_buffer_quantization_index.Offset - 1) % iter.data_buffer_quantization_index.MaxSize;
                        last_index_value = iter.data_buffer_quantization_index.Data[data_index];
                    }

                    auto last_value = ImVec2(0, 0);
                    if (iter.data_buffer_quantization.Offset == 0)
                        last_value = iter.data_buffer_quantization.Data.back();
                    else {
                        auto data_index = (iter.data_buffer_quantization.Offset - 1) % iter.data_buffer_quantization.MaxSize;
                        last_value = iter.data_buffer_quantization.Data[data_index];
                    }

                    if (digital_data_type == 0)
                    {
                        ImPlot::Annotation(last_index_value.x, last_value.y, ImVec4(1.f, 1.f, 1.f, 0.4f), ImVec2(-5.f, -5.f), true, "%s", NumberToBitString(last_index_value.y).c_str());
                    }
                    else if (digital_data_type == 1) {
                        ImPlot::Annotation(last_index_value.x, last_value.y, ImVec4(1.f, 1.f, 1.f, 0.4f), ImVec2(-5.f, -5.f), true, "%s", NumberToHexString(last_index_value.y).c_str());
                    }
                    else if (digital_data_type == 2) {
                        ImPlot::Annotation(last_index_value.x, last_value.y, ImVec4(1.f, 1.f, 1.f, 0.4f), ImVec2(-5.f, -5.f), true, "%d", (int)last_index_value.y);
                    }

                    //render tooltip
                    auto size = iter.data_buffer_quantization_index.Offset == 0 ? (iter.data_buffer_quantization_index.Data.size()) : (iter.data_buffer_quantization_index.Offset % iter.data_buffer_quantization_index.MaxSize);
                    PlotDataTooltip(iter.input_name.c_str(), &iter.data_buffer_quantization_index.Data[0].x, &iter.data_buffer_quantization_index.Data[0].y, 0.25f, size, 2 * sizeof(float));
                }
            }
        }
        int draw_line_index = 0;
        if (show_quant_levels) {
            int number_of_positions = (int)pow(2, quant_bit_depth);
            float min_max_diff = (float)abs(max_quant_value - min_quant_value);
            if (number_of_positions != 0 && number_of_positions < 2000) {
                float qunat_step = min_max_diff / (float)number_of_positions;

                double base = max_quant_value > min_quant_value ? min_quant_value : max_quant_value;
                for (int i = 0; i < number_of_positions; i++) {
                    double pos = (float)(base + i * qunat_step);
                    ImPlot::DragLineY(draw_line_index++, &pos, ImVec4(1, 1, 1, 0.25f), 1, ImPlotDragToolFlags_NoInputs);
                }
            }
        }
        if (show_quant_limits) {
            ImPlot::DragLineY(draw_line_index++, &min_quant_value, ImVec4(1, 1, 1, 1), 1, ImPlotDragToolFlags_NoFit);
            ImPlot::TagY(min_quant_value, ImVec4(1, 1, 1, 1), "Min");

            ImPlot::DragLineY(draw_line_index++, &max_quant_value, ImVec4(1, 1, 1, 1), 1, ImPlotDragToolFlags_NoFit);
            ImPlot::TagY(max_quant_value, ImVec4(1, 1, 1, 1), "Max");
        }
        ImPlot::EndPlot();
    }

    ImPlot::GetStyle() = old_plot_style;
}


template <typename T>
int BinarySearch(const T* arr, int r, T x, int stride) {
    int best_index = 0;
    float best_diff = FLT_MAX;

    for (int i = 0; i < r; i++) {
        auto item = *(T*)((uintptr_t)arr + (uintptr_t)(stride * i));
        float diff = fabs(x - item);

        if (diff <= best_diff && x > item) {
            best_index = i;
            best_diff = diff;
        }
    }

    return best_index;
}


void PlotManager::PlotDataTooltip(const char* name, const float* xs, const float* value, float width_percent, int count, int stride) {

    // get ImGui window DrawList
    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    // calc real value width
    double half_width = width_percent / 2.f;

    // custom tool
    if (ImPlot::IsPlotHovered()) {
        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
        float  tool_l = ImPlot::PlotToPixels(mouse.x - half_width, mouse.y).x;
        float  tool_r = ImPlot::PlotToPixels(mouse.x + half_width, mouse.y).x;
        float  tool_t = ImPlot::GetPlotPos().y;
        float  tool_b = tool_t + ImPlot::GetPlotSize().y;
        ImPlot::PushPlotClipRect();
        draw_list->AddRectFilled(ImVec2(tool_l, tool_t), ImVec2(tool_r, tool_b), IM_COL32(128, 128, 128, 64));
        ImPlot::PopPlotClipRect();
        // find mouse location index
        int idx = BinarySearch<float>(xs, count, (float)mouse.x, stride);
        // render tool tip (won't be affected by plot clip rect)
        if (idx != -1) {
            ImGui::BeginTooltip();

            if (digital_data_type == 0)
            {
                ImGui::Text("%s: %s", name, NumberToBitString(*(float*)((uintptr_t)value + idx * stride)).c_str());
            }
            else if (digital_data_type == 1) {
                ImGui::Text("%s: %s", name, NumberToHexString(*(float*)((uintptr_t)value + idx * stride)).c_str());
            }
            else if (digital_data_type == 2) {
                ImGui::Text("%s: %d", name, (int)*(float*)((uintptr_t)value + idx * stride));
            }
            ImGui::EndTooltip();
        }
    }
}

void PlotManager::RenderDigitalizationOtions()
{
    ImGui::NewLine();
    ImGui::Text("Sampling rate:");
    ImGui::PushItemWidth(-1.f);
    ImGui::SliderFloat("##SamplingRate", &sampling_rate, 1.f, 20.f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::Checkbox("Show sampling", &show_sampling);
    ImGui::NewLine();
    ImGui::Checkbox("Add noise", &add_noise);
    if (add_noise) {
        ImGui::Text("Noise multiplier:");
        ImGui::PushItemWidth(-1.f);
        ImGui::SliderFloat("##NoiseMultiplier", &noise_multiplier, 0.2f, 5.f, "%.2f");
        ImGui::PopItemWidth();
    }
    
    ImGui::NewLine();
    ImGui::Text("Quantization area:");
    ImGui::InputDouble("Min", &min_quant_value, 0.01, 0.1);
    ImGui::InputDouble("Max", &max_quant_value, 0.01, 0.1);
    ImGui::Checkbox("Show quantiz. limits", &show_quant_limits);
    ImGui::Checkbox("Show quantiz. levels", &show_quant_levels);
    ImGui::Text("Quantization bit depth:");
    ImGui::PushItemWidth(-1.f);
    ImGui::SliderInt("##QuantizationBitDepth", &quant_bit_depth, 2, 10);
    ImGui::Checkbox("Show quantizied data", &show_quant_data);
    ImGui::NewLine();
    ImGui::Checkbox("Show digital data", &show_digital_data);
    ImGui::Text("Data type:");
    ImGui::Combo("##digital_data_type", &digital_data_type, "Binary\0Hexadeciaml\0Decimal\0\0");

    ImGui::PopItemWidth();
}

void PlotManager::RenderMainPlotSettings()
{
    ImGui::Checkbox("Pause!", &paused);
    ImGui::SameLine();
    ImGui::Checkbox("Auto size axes", &auto_size);
    ImGui::SliderFloat("Sample point size", &marker_size, 2.f, 5.f, "%.2f");
    ImGui::Text("Sample data render style:");
    ImGui::Combo("##SampleDataStyle", &sample_show_type, "Scatter\0Stem\0\0");
    ImGui::Text("Quantizied data render style:");
    ImGui::Combo("##quantiziedDataStyle", &quant_show_type, "Stairstep\0Line\0\0");
}

void PlotManager::OpenEditInputDialog()
{
    ImGui::OpenPopup("Edit input");
}

void PlotManager::RenderEditInputDialog()
{
    static bool just_opne = true;
    if (ImGui::BeginPopupModal("Edit input", NULL))
    {
        if (edit_input == -1)
            ImGui::CloseCurrentPopup();

        if (edit_input >= 0 && edit_input < inputs.size()) {
            static char name[256] = "Analog_0\0";
            static char math_expression[500] = "\0";
            static int item = 0;
            static float color[4] = { 0.4f, 0.7f, 0.0f, 1.f };
            static float amplitude = 1.f;

            auto& temp = inputs[edit_input];
            if (just_opne) {
                strncpy(name, temp.input_name.c_str(), temp.input_name.size() > 256 ? 255 : temp.input_name.size() + 1);
                name[255] = '\0';

                strncpy(math_expression, temp.math_expr.c_str(), temp.math_expr.size() > 500 ? 499 : temp.math_expr.size() + 1);
                math_expression[499] = '\0';

                item = temp.type;

                color[0] = temp.plot_color.x;
                color[1] = temp.plot_color.y;
                color[2] = temp.plot_color.z;
                color[3] = temp.plot_color.w;

                amplitude = temp.amplitude;
            }

            ImGui::Text("Select input type:");
            ImGui::ComboV("##input_function", &item, vec_function_names);
            ImGui::Text("Input name:");
            ImGui::InputText("##InputNameInput", name, 256);
            ImGui::Text("Plot color:");
            ImGui::ColorEdit4("##PlotColor", color);

            if (item == PlotManager::function_names::math_expression) {
                ImGui::Text("Math expression:");
                ImGui::InputText("##MathExpressionInput", math_expression, 500);
            }
            else {
                ImGui::Text("Amplitude:");
                ImGui::SliderFloat("##AmplitudeSlider", &amplitude, 0.01f, 1.f, "%.2f");
            }

            just_opne = false;
            ImGui::Dummy(ImVec2(300.f, 15.f));

            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
                just_opne = true;
            }

            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.54f, 0.46f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.54f, 0.46f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.54f, 0.46f, 0.7f));
            if (ImGui::Button("Delete")) {
                DeleteInput(edit_input);
                ImGui::CloseCurrentPopup();
                
                ImGui::PopStyleColor(3);
                ImGui::EndPopup();
                just_opne = true;
                return;
            }

            ImGui::PopStyleColor(3);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.98f, 0.48f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.98f, 0.48f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.41f, 0.99f, 0.57f, 0.7f));
            if (ImGui::Button("Save"))
            {
                auto new_name = std::string(name);
                if (CheckIfInputNameExists(new_name, edit_input))
                    new_name += std::string("#") + std::to_string(edit_input);

                temp.input_name = new_name;
                temp.math_expr = std::string(math_expression);
                temp.type = (PlotManager::function_names)item;
                temp.plot_color = ImVec4(color[0], color[1], color[2], color[3]);
                temp.amplitude = amplitude;
                ImGui::CloseCurrentPopup();
                just_opne = true;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::EndPopup();
    }
    else {
        just_opne = true;
    }
}

int PlotManager::RenderAnalogInputCard(Signal& input)
{
    int ret = 0;
    constexpr float aspect_ratio = 9.f / 16.f;
    auto window_width = ImGui::GetCurrentWindow()->Size.x;
    auto card_height = window_width * aspect_ratio;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild(std::string(input.input_name + "##CARD").c_str(), ImVec2(0, card_height), true);
    {
        auto cursor_pos = ImGui::GetCursorPos();

        if (input.data_buffer_analog.Data.size() > 0) {
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
            if (ImPlot::BeginPlot(std::string(input.input_name + "##CARDPLOT").c_str(), ImVec2(window_width, card_height), ImPlotFlags_CanvasOnly | ImPlotFlags_NoChild | ImPlotFlags_NoInputs)) {
                ImPlot::SetupAxisLimits(ImAxis_X1, time - 10.0, time, ImGuiCond_Always);
                ImPlot::SetupAxes(0, 0, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit);
                ImPlot::PushStyleColor(ImPlotCol_Line, input.plot_color);
                ImPlot::PlotLine(std::string(input.input_name + "##CARDPLOTLINE").c_str(), &input.data_buffer_analog.Data[0].x, &input.data_buffer_analog.Data[0].y, input.data_buffer_analog.Data.size(), input.data_buffer_analog.Offset, 2 * sizeof(float));
                ImPlot::PopStyleColor();
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleVar();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.6f));
            ImGui::SetCursorPos(cursor_pos);
            ImGui::Button("Edit", ImVec2(-1.f, card_height / 2.f));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ret = 1;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
            ImGui::SetCursorPos(cursor_pos + ImVec2(0, card_height / 2.f));
            ImGui::Button("Remove", ImVec2(-1.f, card_height / 2.f));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ret = 2;
            }
            ImGui::PopStyleColor(4);
        }

        ImGui::SetCursorPos(cursor_pos + ImVec2(5, 5));
        ImGui::Text(input.input_name.c_str());
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();

    return ret;
}

float PlotManager::GenerateGussianNoise()
{
    // Define random generator with Gaussian distribution
    const float mean = 0.0f;
    const float stddev = 0.1f;
    std::default_random_engine generator;
    std::normal_distribution<float> dist(mean, stddev);
    return dist(generator);
}

float PlotManager::FindClosestQuantValue(float value, float quant_step, int number_of_positions, float min, float max) {
    float closest_value = FLT_MIN;
    float closest_diff = FLT_MAX;

    float base = max > min ? min : max;
    
    for (int i = 0; i < number_of_positions; i++) {
        auto pos = base + quant_step * i;
        auto diff = abs(value - pos);
        if (diff < closest_diff) {
            closest_diff = diff;
            closest_value = pos;
        }
    }

    return closest_value;
}

int PlotManager::FindClosestQuantIndex(float value, float quant_step, int number_of_positions, float min, float max) {
    int closest_index = 0;
    float closest_diff = FLT_MAX;

    float base = max > min ? min : max;

    for (int i = 0; i < number_of_positions; i++) {
        auto pos = base + quant_step * i;
        auto diff = abs(value - pos);
        if (diff < closest_diff) {
            closest_diff = diff;
            closest_index = i;
        }
    }

    return closest_index;
}

void PlotManager::TickOutputData(Signal& output)
{
    auto sampling_rate_in_ms = 1.f / (float)sampling_rate;
    if (output.last_sample_time + sampling_rate_in_ms < time) {
        //sampling
        output.last_sample_time = time;
        //get last valid analog value
        float last_y_axis_value = 0.f;
        if (output.data_buffer_analog.Offset == 0)
            last_y_axis_value = output.data_buffer_analog.Data.back().y;
        else {
            auto data_index = (output.data_buffer_analog.Offset - 1) % output.data_buffer_analog.MaxSize;
            last_y_axis_value = output.data_buffer_analog.Data[data_index].y;
        }
        //add guassian noise
        if (add_noise)
        {
            bool add_or_decreese = rand() % 2;
            if (add_or_decreese)
                last_y_axis_value += GenerateGussianNoise() * noise_multiplier;
            else
                last_y_axis_value -= GenerateGussianNoise() * noise_multiplier;
        }

        output.data_buffer_sampling.AddPoint(time, last_y_axis_value);

        //quantization
        int number_of_positions = (int)pow(2, quant_bit_depth);
        float min_max_diff = (float)abs(max_quant_value - min_quant_value);
        if (number_of_positions != 0) {
            float qunat_step = min_max_diff / (float)number_of_positions;
            
            float quantizied_value = FindClosestQuantValue(last_y_axis_value, qunat_step, number_of_positions, (float)min_quant_value, (float)max_quant_value);
            output.data_buffer_quantization.AddPoint(time, quantizied_value);

            int quant_index = FindClosestQuantIndex(last_y_axis_value, qunat_step, number_of_positions, (float)min_quant_value, (float)max_quant_value);
            output.data_buffer_quantization_index.AddPoint(time, (float)quant_index);
        };
    }
}

void bin(unsigned n, std::string& str)
{
    if (n > 1)
        bin(n >> 1, str);

    if (n & 1)
        str += '1';
    else
        str += '0';
}

std::string PlotManager::NumberToBitString(float number)
{
    int int_num = (int)number;
    unsigned int un_int = abs(int_num);
    std::string out = "";
    bin(un_int, out);
    if (out.length() < quant_bit_depth)
    {
        std::string prefix = "";
        auto diff = quant_bit_depth - out.length();
        for (int i = 0; i < diff; i++) {
            prefix += "0";
        }
        out = prefix + out;
    }
    return out;
}

std::string PlotManager::NumberToHexString(float number)
{
    int int_num = (int)number;
    unsigned int un_int = abs(int_num);
    std::stringstream stream;
    stream << std::hex << un_int;
    std::string result = stream.str();

    //to upper
    for (auto& iter : result) {
        iter = toupper(iter);
    }

    result = "0x" + result;

    return result;
}

bool PlotManager::CheckIfInputNameExists(std::string name, int skip)
{
    bool ret = false;
    for (size_t i = 0; i < inputs.size(); i++) {
        if (skip == (int)i)
            continue;

        auto& iter = inputs[i];

        if (iter.input_name == name) {
            ret = true;
            break;
        }
    }
    return ret;
}

void PlotManager::ProcessMathExpression(Signal& input) {
    float out = 0.f;
    static parser_t parser;

    symbol_table_t symbol_table;
    symbol_table.add_variable("x", time);
    symbol_table.add_constants();

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    if (parser.compile(input.math_expr, expression))
    {
        float result = expression.value();
        input.data_buffer_analog.AddPoint(time, result);
    }
    else {
        printf("Error in expression\n.");
    }
}

void PlotManager::TickInputData(Signal& input)
{
    switch (input.type) {
    case PlotManager::function_names::sin:
        input.data_buffer_analog.AddPoint(time, sinf(time) * input.amplitude);
        break;
    case PlotManager::function_names::cos:
        input.data_buffer_analog.AddPoint(time, cosf(time) * input.amplitude);
        break;
    case PlotManager::function_names::saw:
        break;
    case PlotManager::function_names::sqare:
        break;
    case PlotManager::function_names::random:
        break;
    case PlotManager::function_names::math_expression:
        ProcessMathExpression(input);
        break;
    }
}

void PlotManager::DeleteInput(size_t index)
{
    inputs.erase(inputs.begin() + index);
}
