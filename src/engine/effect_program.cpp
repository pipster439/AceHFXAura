#include "engine/effect_program.h"
#include "engine/input_sources.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string_view>
#include <unordered_map>

namespace aura {
namespace {
using Json = nlohmann::json;
enum class Type { Number, Boolean, Text, Color };
enum class Op { Number, Boolean, Text, Variable, Elapsed, Coordinates, GsiNumber, GsiBool, GsiText, Pressed, Decay,
    Sqrt, Abs, Neg, Ln, Log10, Exp, Pow10, Sin, Cos, Tan, Asin, Acos, Atan, Round, Ceil, Floor,
    Add, Sub, Mul, Div, Mod, Pow, Min, Max, Eq, Ne, Lt, Le, Gt, Ge, Not, And, Or,
    Phase, Wave, Distance, Radial, Rgb, Hsv, Lerp, Brightness, Cycle, AudioFeature, ScreenColor, HallDepth, HallKeyDepth, HallFeature };
enum class Kind { End, Jump, If, Wait, LoopInit, LoopTest, Increment, Action };
enum class Action { Fill, SetKey, SetVar, ChangeVar, Ripple };
enum class Loop { Repeat, Keys, While };
struct Expression {
    Op op{}; Type type{}; std::array<uint32_t, 4> args{}; uint32_t arity{0}, cost{1}, index{0};
    double number{0}; bool boolean{false}; std::string text;
};
struct Instruction {
    Kind kind{}; Action action{}; Loop loop{};
    uint32_t next{0}, yes{0}, no{0}, slot{0}, index{0}, cost{1};
    std::array<uint32_t, 3> expr{}; int key_slot{-1}; bool until{false};
    std::string text, source;
};
struct Value {
    double number{0}; bool boolean{false}; ColorRGB color{}; std::array<char, 257> text{};
};
[[noreturn]] void Invalid(const std::string& path, const std::string& message, const std::string& node = {}) {
    throw EffectProgramException({"invalid_program", path, node, message});
}
void Fields(const Json& value, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional, const std::string& path) {
    if (!value.is_object()) Invalid(path, "Expected an object");
    for (const char* field : required) if (!value.contains(field)) Invalid(path + "/" + field, "Missing field");
    for (auto it = value.begin(); it != value.end(); ++it) {
        bool known = false;
        for (const char* field : required) known |= it.key() == field;
        for (const char* field : optional) known |= it.key() == field;
        if (!known) Invalid(path + "/" + it.key(), "Unsupported field");
    }
}
uint32_t UInt(const Json& value, uint32_t maximum, const std::string& path) {
    if ((!value.is_number_integer() && !value.is_number_unsigned()) || value < 0 || value > maximum)
        Invalid(path, "Expected bounded nonnegative integer");
    return value.get<uint32_t>();
}
std::string Text(const Json& value, size_t maximum, const std::string& path) {
    if (!value.is_string()) Invalid(path, "Expected string");
    auto text = value.get<std::string>();
    if (text.size() > maximum || text.find('\0') != std::string::npos) Invalid(path, "String exceeds bounds or contains NUL");
    return text;
}
bool In(std::string_view text, std::initializer_list<std::string_view> values) {
    return std::find(values.begin(), values.end(), text) != values.end();
}
struct HallUsageNameEntry { const char* name; uint16_t hid_usage; };
constexpr std::array<HallUsageNameEntry, 75> kHallKeyUsages = {{
    {"ESC", 41}, {"1", 30}, {"2", 31}, {"3", 32}, {"4", 33}, {"5", 34}, {"6", 35},
    {"7", 36}, {"8", 37}, {"9", 38}, {"0", 39}, {"-", 45}, {"=", 46}, {"BACKSPACE", 42}, {"INS", 73},
    {"TAB", 43}, {"Q", 20}, {"W", 26}, {"E", 8}, {"R", 21}, {"T", 23}, {"Y", 28},
    {"U", 24}, {"I", 12}, {"O", 18}, {"P", 19}, {"[", 47}, {"]", 48}, {"\\", 49}, {"DEL", 76},
    {"CAPS", 57}, {"A", 4}, {"S", 22}, {"D", 7}, {"F", 9}, {"G", 10}, {"H", 11},
    {"J", 13}, {"K", 14}, {"L", 15}, {";", 51}, {"'", 52}, {"ENTER", 40}, {"PGUP", 75},
    {"L_SHIFT", 225}, {"LSHIFT", 225}, {"Z", 29}, {"X", 27}, {"C", 6}, {"V", 25}, {"B", 5}, {"N", 17},
    {"M", 16}, {",", 54}, {".", 55}, {"/", 56}, {"R_SHIFT", 229}, {"RSHIFT", 229}, {"UP", 82}, {"PGDN", 78},
    {"L_CTRL", 224}, {"LCTRL", 224}, {"L_WIN", 227}, {"LWIN", 227}, {"L_ALT", 226}, {"LALT", 226},
    {"SPACE", 44}, {"R_ALT", 230}, {"RALT", 230}, {"FN", 232}, {"COPILOT", 228},
    {"LEFT", 80}, {"DOWN", 81}, {"RIGHT", 79}, {"RETURN", 40}
}};
uint16_t ResolveHallUsage(const std::string& key_name) {
    if (key_name.empty()) return 0;
    for (const auto& entry : kHallKeyUsages) {
        std::string_view a(entry.name);
        if (a.size() != key_name.size()) continue;
        bool match = true;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = a[i];
            char cb = key_name[i];
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
            if (ca != cb) { match = false; break; }
        }
        if (match) return entry.hid_usage;
    }
    return 0;
}
uint8_t Byte(double value) { return static_cast<uint8_t>(std::clamp(value, 0.0, 255.0)); }
ColorRGB Scale(ColorRGB color, double factor) {
    factor = std::clamp(factor, 0.0, 1.0);
    return {Byte(color.r * factor), Byte(color.g * factor), Byte(color.b * factor)};
}
ColorRGB Blend(ColorRGB a, ColorRGB b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {Byte(a.r * (1-t) + b.r * t), Byte(a.g * (1-t) + b.g * t), Byte(a.b * (1-t) + b.b * t)};
}
struct RuntimeFault { const char* code; const char* message; };
void Finite(double value) { if (!std::isfinite(value)) throw RuntimeFault{"non_finite", "Numeric expression produced a non-finite value"}; }
}

struct EffectProgram::Impl {
    std::vector<Expression> expressions;
    std::vector<Instruction> instructions;
    uint32_t entry{0}, variables{0}, slots{0}, fade{0};
    bool one_shot{false}, keyboard{false}, gsi{false}, audio{false}, screen{false}, hall{false};
};
namespace {
class Compiler {
public:
    std::shared_ptr<EffectProgram::Impl> program = std::make_shared<EffectProgram::Impl>();
    uint32_t node_count{0};
    uint32_t Expr(const Json& value, const std::string& path, uint32_t depth = 0) {
        if (depth > 32 || program->expressions.size() >= EffectProgram::MaxExpressions) Invalid(path, "Expression bounds exceeded");
        if (!value.is_object() || !value.contains("op")) Invalid(path, "Expression requires op");
        const auto op = Text(value.at("op"), 32, path + "/op");
        Expression e;
        if (op == "number" || op == "boolean" || op == "text") {
            Fields(value, {"op", "value"}, {}, path);
            if (op == "number") {
                if (!value["value"].is_number()) Invalid(path, "Number literal expected");
                e.number = value["value"].get<double>(); if (!std::isfinite(e.number)) Invalid(path, "Non-finite literal");
                e.op = Op::Number; e.type = Type::Number;
            } else if (op == "boolean") {
                if (!value["value"].is_boolean()) Invalid(path, "Boolean literal expected");
                e.op = Op::Boolean; e.type = Type::Boolean; e.boolean = value["value"].get<bool>();
            } else { e.op = Op::Text; e.type = Type::Text; e.text = Text(value["value"], 256, path); }
        } else if (op == "var") {
            Fields(value, {"op", "index"}, {}, path);
            e.op = Op::Variable; e.type = Type::Number;
            e.index = UInt(value["index"], program->variables ? program->variables - 1 : 0, path);
            if (!program->variables) Invalid(path, "Variable used without declaration");
        } else if (op == "elapsed" || op == "pressed") {
            Fields(value, {"op"}, {}, path); e.op = op == "elapsed" ? Op::Elapsed : Op::Pressed;
            e.type = op == "elapsed" ? Type::Number : Type::Boolean; program->keyboard |= op == "pressed";
        } else if (op == "coords") {
            Fields(value, {"op", "field"}, {}, path); e.text = Text(value["field"], 32, path);
            if (!In(e.text, {"physical_x", "physical_y", "row", "col", "led_id"})) Invalid(path, "Unknown coordinate");
            e.op = Op::Coordinates; e.type = Type::Number;
        } else if (In(op, {"gsi_number", "gsi_bool", "gsi_text"})) {
            Fields(value, {"op", "path", "default"}, {}, path); e.text = Text(value["path"], 128, path);
            if (e.text.empty() || e.text.rfind("event.", 0) == 0 || e.text.rfind("event_sequence.", 0) == 0 ||
                !std::all_of(e.text.begin(), e.text.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; })) Invalid(path, "Unsupported GSI path");
            e.op = op == "gsi_number" ? Op::GsiNumber : op == "gsi_bool" ? Op::GsiBool : Op::GsiText;
            e.type = op == "gsi_number" ? Type::Number : op == "gsi_bool" ? Type::Boolean : Type::Text;
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != e.type) Invalid(path, "GSI default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->gsi = true;
        } else if (op == "audio_feature") {
            Fields(value, {"op", "feature", "default"}, {}, path);
            e.text = Text(value["feature"], 32, path + "/feature");
            if (!In(e.text, {"rms", "peak", "centroid"})) Invalid(path, "Unknown audio feature");
            e.op = Op::AudioFeature; e.type = Type::Number;
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != Type::Number) Invalid(path, "Audio feature default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->audio = true;
        } else if (op == "screen_color") {
            Fields(value, {"op", "default"}, {}, path);
            e.op = Op::ScreenColor; e.type = Type::Color;
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != Type::Color) Invalid(path, "Screen color default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->screen = true;
        } else if (op == "hall_depth") {
            Fields(value, {"op", "default"}, {"unit"}, path);
            std::string unit = "normalized";
            if (value.contains("unit")) {
                unit = Text(value["unit"], 16, path + "/unit");
                if (!In(unit, {"normalized", "mm"})) Invalid(path + "/unit", "Unknown Hall depth unit");
            }
            e.op = Op::HallDepth; e.type = Type::Number; e.boolean = (unit == "mm");
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != Type::Number) Invalid(path, "Hall depth default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->hall = true;
        } else if (op == "hall_key_depth") {
            Fields(value, {"op", "key", "default"}, {"unit"}, path);
            e.text = Text(value["key"], 32, path + "/key");
            const uint16_t usage = ResolveHallUsage(e.text);
            if (usage == 0) Invalid(path + "/key", "Unknown Hall key name");
            std::string unit = "normalized";
            if (value.contains("unit")) {
                unit = Text(value["unit"], 16, path + "/unit");
                if (!In(unit, {"normalized", "mm"})) Invalid(path + "/unit", "Unknown Hall depth unit");
            }
            e.op = Op::HallKeyDepth; e.type = Type::Number; e.index = usage; e.boolean = (unit == "mm");
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != Type::Number) Invalid(path, "Hall key depth default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->hall = true;
        } else if (op == "hall_feature") {
            Fields(value, {"op", "feature", "default"}, {}, path);
            e.text = Text(value["feature"], 32, path + "/feature");
            if (!In(e.text, {"max_depth", "avg_depth", "active_count", "active"})) Invalid(path + "/feature", "Unknown Hall feature");
            e.op = Op::HallFeature; e.type = Type::Number;
            e.args[0] = Expr(value["default"], path + "/default", depth + 1); e.arity = 1;
            if (program->expressions[e.args[0]].type != Type::Number) Invalid(path, "Hall feature default type mismatch");
            e.cost += program->expressions[e.args[0]].cost; program->hall = true;
        } else {
            Fields(value, {"op", "args"}, {"wave"}, path);
            struct Spec { Op op; uint32_t arity; Type result; };
            static const std::unordered_map<std::string, Spec> specs = {
                {"sqrt",{Op::Sqrt,1,Type::Number}},{"abs",{Op::Abs,1,Type::Number}},{"neg",{Op::Neg,1,Type::Number}},
                {"ln",{Op::Ln,1,Type::Number}},{"log10",{Op::Log10,1,Type::Number}},{"exp",{Op::Exp,1,Type::Number}},
                {"pow10",{Op::Pow10,1,Type::Number}},{"sin",{Op::Sin,1,Type::Number}},{"cos",{Op::Cos,1,Type::Number}},
                {"tan",{Op::Tan,1,Type::Number}},{"asin",{Op::Asin,1,Type::Number}},{"acos",{Op::Acos,1,Type::Number}},
                {"atan",{Op::Atan,1,Type::Number}},{"round",{Op::Round,1,Type::Number}},{"ceil",{Op::Ceil,1,Type::Number}},
                {"floor",{Op::Floor,1,Type::Number}},{"add",{Op::Add,2,Type::Number}},{"sub",{Op::Sub,2,Type::Number}},
                {"mul",{Op::Mul,2,Type::Number}},{"div",{Op::Div,2,Type::Number}},{"mod",{Op::Mod,2,Type::Number}},
                {"pow",{Op::Pow,2,Type::Number}},{"min",{Op::Min,2,Type::Number}},{"max",{Op::Max,2,Type::Number}},
                {"eq",{Op::Eq,2,Type::Boolean}},{"ne",{Op::Ne,2,Type::Boolean}},{"lt",{Op::Lt,2,Type::Boolean}},
                {"le",{Op::Le,2,Type::Boolean}},{"gt",{Op::Gt,2,Type::Boolean}},{"ge",{Op::Ge,2,Type::Boolean}},
                {"not",{Op::Not,1,Type::Boolean}},{"and",{Op::And,2,Type::Boolean}},{"or",{Op::Or,2,Type::Boolean}},
                {"time_phase",{Op::Phase,1,Type::Number}},{"waveform",{Op::Wave,3,Type::Number}},
                {"distance",{Op::Distance,4,Type::Number}},{"radial_phase",{Op::Radial,4,Type::Number}},
                {"rgb",{Op::Rgb,3,Type::Color}},{"hsv",{Op::Hsv,3,Type::Color}},{"lerp",{Op::Lerp,3,Type::Color}},
                {"brightness",{Op::Brightness,2,Type::Color}},{"cycle",{Op::Cycle,3,Type::Color}},{"decay",{Op::Decay,1,Type::Number}}
            };
            auto found = specs.find(op); if (found == specs.end()) Invalid(path, "Unknown expression operation");
            e.op = found->second.op; e.type = found->second.result; e.arity = found->second.arity;
            if (!value["args"].is_array() || value["args"].size() != e.arity) Invalid(path, "Expression arity mismatch");
            for (uint32_t i = 0; i < e.arity; ++i) { e.args[i] = Expr(value["args"][i], path + "/args/" + std::to_string(i), depth + 1); e.cost += program->expressions[e.args[i]].cost; }
            if (e.op == Op::Wave) {
                if (!value.contains("wave")) Invalid(path, "Waveform needs wave kind");
                e.text = Text(value["wave"], 16, path);
                if (!In(e.text, {"sine", "triangle", "square", "saw"})) Invalid(path, "Unknown waveform");
            } else if (value.contains("wave")) Invalid(path, "wave is only valid for waveform");
            for (uint32_t i = 0; i < e.arity; ++i) {
                const auto type = program->expressions[e.args[i]].type;
                Type expected = Type::Number;
                if (e.op == Op::Not || e.op == Op::And || e.op == Op::Or) expected = Type::Boolean;
                if ((e.op == Op::Lerp || e.op == Op::Cycle) && i < 2) expected = Type::Color;
                if (e.op == Op::Brightness && i == 0) expected = Type::Color;
                if (e.op == Op::Eq || e.op == Op::Ne) {
                    expected = program->expressions[e.args[0]].type;
                    if (expected == Type::Color) Invalid(path, "Color equality is unsupported");
                }
                if (type != expected) Invalid(path, "Expression argument type mismatch");
            }
            program->keyboard |= e.op == Op::Decay;
        }
        if (e.cost >= EffectProgram::FrameBudget || program->expressions.size() >= EffectProgram::MaxExpressions)
            Invalid(path, "Expression cannot fit in one frame budget");
        program->expressions.push_back(std::move(e)); return static_cast<uint32_t>(program->expressions.size() - 1);
    }
    void Node(const Json& value, uint32_t index) {
        const auto path = "/nodes/" + std::to_string(index);
        if (!value.is_object() || !value.contains("kind")) Invalid(path, "Instruction requires kind");
        const auto kind = Text(value["kind"], 20, path); Instruction n;
        if (value.contains("source")) n.source = Text(value["source"], 128, path);
        try {
            if (value.contains("keySlot")) { if (!program->slots) Invalid(path, "No declared slots"); n.key_slot = static_cast<int>(UInt(value["keySlot"], program->slots - 1, path)); }
            auto pc = [&](const char* field) { return UInt(value.at(field), node_count - 1, path + "/" + field); };
            auto slot = [&] { if (!program->slots) Invalid(path, "No declared slots"); return UInt(value.at("slot"), program->slots - 1, path); };
            auto expr = [&](const char* field, uint32_t at, Type type) { n.expr[at] = Expr(value.at(field), path + "/" + field); const auto& e = program->expressions[n.expr[at]]; if (e.type != type) Invalid(path, "Instruction expression type mismatch"); n.cost += e.cost; };
            if (kind == "end") { Fields(value, {"kind"}, {"source", "keySlot"}, path); n.kind = Kind::End; }
            else if (kind == "jump") { Fields(value, {"kind","next"}, {"source","keySlot"}, path); n.kind=Kind::Jump; n.next=pc("next"); }
            else if (kind == "if") { Fields(value, {"kind","condition","yes","no"}, {"source","keySlot"}, path); n.kind=Kind::If; n.yes=pc("yes");n.no=pc("no");expr("condition",0,Type::Boolean); }
            else if (kind == "wait") { Fields(value, {"kind","duration","next"}, {"source","keySlot"}, path); n.kind=Kind::Wait;n.next=pc("next");expr("duration",0,Type::Number); }
            else if (kind == "increment") { Fields(value,{"kind","slot","next"},{"source","keySlot"},path); n.kind=Kind::Increment;n.slot=slot();n.next=pc("next"); }
            else if (kind == "loopInit") {
                Fields(value,{"kind","slot","mode","next"},{"count","zone","source","keySlot"},path);
                n.kind=Kind::LoopInit;n.slot=slot();n.next=pc("next");
                const auto mode=Text(value["mode"],16,path);
                if(mode=="repeat") { if(!value.contains("count") || value.contains("zone")) Invalid(path,"repeat requires count only"); n.loop=Loop::Repeat;expr("count",0,Type::Number); }
                else if(mode=="keys") { if(value.contains("count")) Invalid(path,"keys has no count");n.loop=Loop::Keys; }
                else Invalid(path,"Unknown loop initialization");
            } else if (kind == "loopTest") {
                Fields(value,{"kind","slot","mode","body","after"},{"condition","until","zone","source","keySlot"},path);
                n.kind=Kind::LoopTest;n.slot=slot();n.yes=pc("body");n.no=pc("after");
                const auto mode=Text(value["mode"],16,path);
                if(mode=="while") { n.loop=Loop::While;expr("condition",0,Type::Boolean);if(value.contains("until")){if(!value["until"].is_boolean())Invalid(path,"until must be boolean");n.until=value["until"].get<bool>();} }
                else if(mode=="keys" || mode=="repeat") { n.loop=mode=="keys"?Loop::Keys:Loop::Repeat;if(value.contains("condition")||value.contains("until"))Invalid(path,"Condition only valid for while"); }
                else Invalid(path,"Unknown loop test");
            } else if (kind == "action") {
                if(!value.contains("op"))Invalid(path,"Action requires op");const auto action=Text(value["op"],20,path);n.kind=Kind::Action;n.next=pc("next");
                if(action=="fill") {Fields(value,{"kind","op","color","next"},{"source","keySlot"},path);n.action=Action::Fill;expr("color",0,Type::Color);n.cost+=TOTAL_LEDS;}
                else if(action=="setKey") {Fields(value,{"kind","op","color","led","next"},{"source","keySlot"},path);n.action=Action::SetKey;expr("color",0,Type::Color);expr("led",1,Type::Number);}
                else if(action=="setVar"||action=="changeVar") {Fields(value,{"kind","op","index","value","next"},{"source","keySlot"},path);if(!program->variables)Invalid(path,"No variables");n.action=action=="setVar"?Action::SetVar:Action::ChangeVar;n.index=UInt(value["index"],program->variables-1,path);expr("value",0,Type::Number);}
                else if(action=="ripple") {Fields(value,{"kind","op","key","speed","color","next"},{"source","keySlot"},path);n.action=Action::Ripple;n.text=Text(value["key"],15,path);expr("color",0,Type::Color);expr("speed",1,Type::Number);n.cost+=128;}
                else Invalid(path,"Unknown action");
            } else Invalid(path,"Unknown instruction");
            if(value.contains("zone")) {if(n.loop!=Loop::Keys)Invalid(path,"zone requires key loop");n.text=Text(value["zone"],16,path);if(!In(n.text,{"all","alpha","wasd","arrows","numeric","navigation","modifier"}))Invalid(path,"Unknown key zone");}
            if(n.cost>EffectProgram::FrameBudget)Invalid(path,"Instruction cannot fit frame budget");
            program->instructions.push_back(std::move(n));
        } catch(const EffectProgramException& e) {auto error=e.Error();error.node=n.source;throw EffectProgramException(std::move(error));}
        catch(const Json::exception&) {Invalid(path,"Missing or invalid instruction field",n.source);}
    }
};
}

std::shared_ptr<const EffectProgram> EffectProgram::Compile(const Json& document) {
    Fields(document,{"schema_version","entry","variables","slots","publication","nodes"},{},"");
    if(UInt(document["schema_version"],1,"/schema_version")!=1)Invalid("/schema_version","Unsupported program version");
    if(!document["nodes"].is_array()||document["nodes"].empty()||document["nodes"].size()>MaxInstructions)Invalid("/nodes","Invalid instruction count");
    Compiler compiler;compiler.node_count=static_cast<uint32_t>(document["nodes"].size());auto& p=*compiler.program;
    p.entry=UInt(document["entry"],static_cast<uint32_t>(document["nodes"].size()-1),"/entry");
    p.variables=UInt(document["variables"],MaxVariables,"/variables");p.slots=UInt(document["slots"],MaxSlots,"/slots");
    const auto& publication=document["publication"];Fields(publication,{"mode","fade_out_ms"},{},"/publication");
    const auto mode=Text(publication["mode"],16,"/publication/mode");if(!In(mode,{"continuous","one_shot"}))Invalid("/publication","Unknown lifecycle");
    p.one_shot=mode=="one_shot";p.fade=UInt(publication["fade_out_ms"],60000,"/publication/fade_out_ms");
    if(!p.one_shot&&p.fade)Invalid("/publication","Continuous publication cannot fade");
    p.instructions.reserve(document["nodes"].size());
    for(uint32_t i=0;i<document["nodes"].size();++i)compiler.Node(document["nodes"][i],i);
    // Verify exact targets independently of vector implementation capacity.
    for(const auto& n:p.instructions) {
        if((n.kind!=Kind::End && n.kind!=Kind::If && n.kind!=Kind::LoopTest && n.next>=p.instructions.size()) ||
           ((n.kind==Kind::If || n.kind==Kind::LoopTest)&&(n.yes>=p.instructions.size()||n.no>=p.instructions.size()))) Invalid("/nodes","PC target out of range",n.source);
    }
    // Every accepted subtree is now bounded by the typed compiler; do not dump
    // an unvalidated deeply nested value just to measure its serialized size.
    try { if(document.dump().size()>MaxJsonBytes)Invalid("","Program exceeds 1 MiB"); }
    catch(const Json::exception&) {Invalid("","Program contains invalid JSON text encoding");}
    return std::shared_ptr<const EffectProgram>(new EffectProgram(std::move(compiler.program)));
}
std::shared_ptr<const EffectProgram> EffectProgram::Parse(const std::string& document) {
    if(document.size()>MaxJsonBytes)Invalid("","Program exceeds 1 MiB");
    try {
        std::vector<std::set<std::string>> keys;
        auto parsed=Json::parse(document,[&keys](int depth,Json::parse_event_t event,Json& value){
            if(depth>80)Invalid("","JSON nesting too deep");
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            else if(event==Json::parse_event_t::object_end)keys.pop_back();
            else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)Invalid("","Duplicate JSON field");
            return true;
        });
        return Compile(parsed);
    } catch(const EffectProgramException&){throw;} catch(const Json::exception&){Invalid("","Malformed JSON program");}
}
bool EffectProgram::UsesKeyboard()const{return impl_->keyboard;}
bool EffectProgram::UsesGsi()const{return impl_->gsi;}
bool EffectProgram::UsesAudio()const{return impl_->audio;}
bool EffectProgram::UsesScreen()const{return impl_->screen;}
bool EffectProgram::UsesHall()const{return impl_->hall;}
uint32_t EffectProgram::InstructionCount()const{return static_cast<uint32_t>(impl_->instructions.size());}

struct ProgramEffect::Runtime {
    std::shared_ptr<const EffectProgram::Impl> program;
    uint64_t generation_id{0};
    AuraLogicalKeyboardHostV1 host{};
    AuraLogicalKeyboardSnapshotV1 keyboard{};
    AuraHallAnalogHostV1 hall_host{};
    AuraHallAnalogSnapshotV1 hall_snapshot{};
    std::array<const KeyInfo*, 128> keys{};
    std::array<uint16_t, 128> key_usages{};
    uint32_t current_key_index{UINT32_MAX};
    uint32_t key_count{0}; const Keymap* keymap{nullptr};
    struct State {
        std::array<double, EffectProgram::MaxVariables> variables{};
        std::array<uint32_t, EffectProgram::MaxSlots> indices{}, limits{};
        FrameBuffer frame; std::bitset<TOTAL_LEDS> coverage;
        uint32_t pc{0}; uint64_t wake{0}, last{0}, terminal_at{0};
        bool initialized{false}, terminal{false}, finished{false}, zero_rendered{false}; float opacity{1};
    } state;
    EffectProgramStatus status;
    const KeyInfo* current_key{nullptr}; const EffectContext* context{nullptr};

    void CacheKeys(const Keymap& map) {
        if(keymap==&map)return;
        keys.fill(nullptr);key_count=0;
        key_usages.fill(0);
        for(const auto& name:Keymap::GetStandardLayoutKeys()) {
            if(key_count==keys.size())throw RuntimeFault{"keymap_limit","Standard layout exceeds bounded key capacity"};
            auto found=map.GetAllKeys().find(name);
            keys[key_count]=found==map.GetAllKeys().end()?nullptr:&found->second;
            key_usages[key_count]=ResolveHallUsage(name);
            ++key_count;
        }
        keymap=&map;
    }
    void Keyboard() {
        keyboard={};
        if(program->keyboard && host.read_snapshot && host.read_snapshot(host.context,&keyboard,sizeof(keyboard))==1 &&
           keyboard.struct_size==sizeof(keyboard) && keyboard.version==AURA_LOGICAL_KEYBOARD_VERSION &&
           keyboard.count<=AURA_LOGICAL_KEYBOARD_MAX_KEYS && keyboard.source_available==1) {
            for(uint32_t i=0;i<keyboard.count;++i)
                if(!std::memchr(keyboard.keys[i].name,0,AURA_LOGICAL_KEYBOARD_NAME_BYTES)) {keyboard={};break;}
        } else keyboard={};
        status.keyboard_available=keyboard.source_available==1;
    }
    void Hall() {
        hall_snapshot={};
        if(program->hall && hall_host.read_snapshot &&
           hall_host.read_snapshot(hall_host.context,&hall_snapshot,sizeof(hall_snapshot))==1 &&
           hall_snapshot.struct_size==sizeof(hall_snapshot) && hall_snapshot.version==AURA_HALL_ANALOG_VERSION &&
           hall_snapshot.count<=AURA_HALL_ANALOG_MAX_KEYS && hall_snapshot.active==1) {
            status.hall_available=true;
        } else {
            hall_snapshot.active=0; hall_snapshot.count=0; status.hall_available=false;
        }
    }
    const AuraHallKeyStateV1* HallKeyStateByUsage(uint16_t usage) const {
        if(!usage || !hall_snapshot.active)return nullptr;
        for(uint32_t i=0;i<hall_snapshot.count;++i)
            if(hall_snapshot.keys[i].hid_usage==usage)return &hall_snapshot.keys[i];
        return nullptr;
    }
    const AuraLogicalKeyStateV1* KeyState() const {
        if(!current_key || !keyboard.source_available)return nullptr;
        for(uint32_t i=0;i<keyboard.count;++i)
            if(current_key->key_name==keyboard.keys[i].name)return &keyboard.keys[i];
        return nullptr;
    }
    static void CopyText(Value& output,const char* text) {
        if(!text)text="";
        size_t length=0;while(length<output.text.size()&&text[length])++length;
        if(length>=output.text.size())throw RuntimeFault{"text_limit","GSI text exceeds 256 bytes"};
        std::memcpy(output.text.data(),text,length+1);
    }
    Value Eval(uint32_t id) {
        const auto& e=program->expressions[id];Value result;
        switch(e.op) {
            case Op::Number:result.number=e.number;return result;
            case Op::Boolean:result.boolean=e.boolean;return result;
            case Op::Text:CopyText(result,e.text.c_str());return result;
            case Op::Variable:result.number=state.variables[e.index];return result;
            case Op::Elapsed:result.number=static_cast<double>(context->elapsed_ms);return result;
            case Op::Coordinates:
                if(current_key)result.number=e.text=="physical_x"?current_key->physical_x:e.text=="physical_y"?current_key->physical_y:
                    e.text=="row"?current_key->physical_row:e.text=="col"?current_key->physical_col:current_key->led_id;
                else result.number=e.text=="led_id"?-1:0;
                Finite(result.number);return result;
            case Op::Pressed:{const auto* key=KeyState();result.boolean=key&&key->held==1;return result;}
            case Op::GsiNumber:case Op::GsiBool:case Op::GsiText:{
                result=Eval(e.args[0]);if(!context->gsi || !context->gsi->IsActive())return result;
                if(e.op==Op::GsiNumber){result.number=context->gsi->GetNumber(e.text.c_str(),result.number);Finite(result.number);}
                else if(e.op==Op::GsiBool)result.boolean=context->gsi->GetBool(e.text.c_str(),result.boolean);
                else {Value copy;CopyText(copy,context->gsi->GetString(e.text.c_str(),result.text.data()));result=copy;}
                return result;
            }
            case Op::AudioFeature:{
                result=Eval(e.args[0]);
                if(context && context->audio) {
                    const auto reading = context->audio->Read(context->monotonic_ms);
                    if(reading.health.availability == InputAvailability::Fresh) {
                        result.number = (e.text == "peak") ? reading.sample.peak :
                                        (e.text == "centroid") ? reading.sample.spectral_centroid_hz :
                                        reading.sample.rms;
                        Finite(result.number);
                    }
                }
                return result;
            }
            case Op::ScreenColor:{
                result=Eval(e.args[0]);
                if(context && context->screen) {
                    const auto reading = context->screen->Read(context->monotonic_ms);
                    if(reading.health.availability == InputAvailability::Fresh) {
                        result.color = reading.sample.color;
                    }
                }
                return result;
            }
            case Op::HallDepth:{
                result=Eval(e.args[0]);
                if(hall_snapshot.active && current_key_index < key_count) {
                    if(const auto* hk=HallKeyStateByUsage(key_usages[current_key_index])) {
                        result.number = e.boolean ? std::clamp(static_cast<double>(hk->depth_mm), 0.0, 4.0)
                                                  : std::clamp(static_cast<double>(hk->normalized), 0.0, 1.0);
                        Finite(result.number);
                    }
                }
                return result;
            }
            case Op::HallKeyDepth:{
                result=Eval(e.args[0]);
                if(const auto* hk=HallKeyStateByUsage(static_cast<uint16_t>(e.index))) {
                    result.number = e.boolean ? std::clamp(static_cast<double>(hk->depth_mm), 0.0, 4.0)
                                              : std::clamp(static_cast<double>(hk->normalized), 0.0, 1.0);
                    Finite(result.number);
                }
                return result;
            }
            case Op::HallFeature:{
                result=Eval(e.args[0]);
                if(!hall_snapshot.active) return result;
                if(e.text=="active") { result.number = 1.0; return result; }
                double max_n = 0.0, sum_n = 0.0; uint32_t active_cnt = 0;
                for(uint32_t i=0;i<hall_snapshot.count;++i) {
                    const double norm = std::clamp(static_cast<double>(hall_snapshot.keys[i].normalized), 0.0, 1.0);
                    if(norm > max_n) max_n = norm;
                    sum_n += norm;
                    if(hall_snapshot.keys[i].depth_mm >= 0.08f) ++active_cnt;
                }
                if(e.text=="max_depth") result.number = max_n;
                else if(e.text=="avg_depth") result.number = hall_snapshot.count ? (sum_n / hall_snapshot.count) : 0.0;
                else if(e.text=="active_count") result.number = static_cast<double>(active_cnt);
                Finite(result.number);
                return result;
            }
            default:break;
        }
        const auto a=Eval(e.args[0]);
        if(e.op==Op::And&&!a.boolean){result.boolean=false;return result;}
        if(e.op==Op::Or&&a.boolean){result.boolean=true;return result;}
        Value b,c,d;if(e.arity>1)b=Eval(e.args[1]);if(e.arity>2)c=Eval(e.args[2]);if(e.arity>3)d=Eval(e.args[3]);
        const double x=a.number,y=b.number,z=c.number,w=d.number;
        constexpr double pi=3.14159265358979323846;
        switch(e.op) {
            case Op::Sqrt:result.number=std::sqrt(x);break;case Op::Abs:result.number=std::fabs(x);break;case Op::Neg:result.number=-x;break;
            case Op::Ln:result.number=std::log(x);break;case Op::Log10:result.number=std::log10(x);break;case Op::Exp:result.number=std::exp(x);break;
            case Op::Pow10:result.number=std::pow(10.0,x);break;case Op::Sin:result.number=std::sin(x);break;case Op::Cos:result.number=std::cos(x);break;
            case Op::Tan:result.number=std::tan(x);break;case Op::Asin:result.number=std::asin(x);break;case Op::Acos:result.number=std::acos(x);break;
            case Op::Atan:result.number=std::atan(x);break;case Op::Round:result.number=std::round(x);break;case Op::Ceil:result.number=std::ceil(x);break;case Op::Floor:result.number=std::floor(x);break;
            case Op::Add:result.number=x+y;break;case Op::Sub:result.number=x-y;break;case Op::Mul:result.number=x*y;break;
            case Op::Div:if(y==0)throw RuntimeFault{"divide_by_zero","Division by zero"};result.number=x/y;break;
            case Op::Mod:if(y==0)throw RuntimeFault{"divide_by_zero","Modulo by zero"};result.number=std::fmod(x,y);break;
            case Op::Pow:result.number=std::pow(x,y);break;case Op::Min:result.number=std::min(x,y);break;case Op::Max:result.number=std::max(x,y);break;
            case Op::Eq:case Op::Ne:{const auto type=program->expressions[e.args[0]].type;
                const bool equal=type==Type::Number?x==y:type==Type::Boolean?a.boolean==b.boolean:std::strcmp(a.text.data(),b.text.data())==0;
                result.boolean=e.op==Op::Eq?equal:!equal;break;}
            case Op::Lt:result.boolean=x<y;break;case Op::Le:result.boolean=x<=y;break;case Op::Gt:result.boolean=x>y;break;case Op::Ge:result.boolean=x>=y;break;
            case Op::Not:result.boolean=!a.boolean;break;case Op::And:result.boolean=a.boolean&&b.boolean;break;case Op::Or:result.boolean=a.boolean||b.boolean;break;
            case Op::Phase:result.number=std::fmod(static_cast<double>(context->elapsed_ms),std::max(33.0,x))/std::max(33.0,x);break;
            case Op::Wave:{const double phase=std::fmod(x,1.0);result.number=(e.text=="sine"?std::sin(x*2*pi):e.text=="triangle"?std::fabs(phase*2-1)*2-1:e.text=="square"?(phase<.5?1.0:-1.0):phase*2-1)*y+z;break;}
            case Op::Distance:result.number=std::hypot(x-z,y-w);break;
            case Op::Radial:result.number=std::fmod(std::hypot((current_key?current_key->physical_x:0)-x,(current_key?current_key->physical_y:0)-y)/std::max(.1,z)-(context->elapsed_ms/1000.0)*w,1.0);break;
            case Op::Rgb:result.color={Byte(x),Byte(y),Byte(z)};break;
            case Op::Hsv:{double hue=std::fmod(x,360.0);if(hue<0)hue+=360;const double saturation=std::clamp(y/100.0,0.0,1.0),v=std::clamp(z/100.0,0.0,1.0),chroma=v*saturation;
                const double secondary=chroma*(1-std::fabs(std::fmod(hue/60,2.0)-1)),m=v-chroma;double r=0,g=0,blue=0;
                if(hue<60){r=chroma;g=secondary;}else if(hue<120){r=secondary;g=chroma;}else if(hue<180){g=chroma;blue=secondary;}else if(hue<240){g=secondary;blue=chroma;}else if(hue<300){r=secondary;blue=chroma;}else{r=chroma;blue=secondary;}
                result.color={Byte((r+m)*255),Byte((g+m)*255),Byte((blue+m)*255)};break;}
            case Op::Lerp:result.color=Blend(a.color,b.color,z);break;
            case Op::Brightness:result.color=Scale(a.color,y);break;
            case Op::Cycle:{const double period=std::max(100.0,z*1000);const double phase=std::fmod(static_cast<double>(context->elapsed_ms),period)/period;result.color=Blend(a.color,b.color,.5-.5*std::cos(phase*2*pi));break;}
            case Op::Decay:{const auto* key=KeyState();if(key&&key->held==1)result.number=1;
                else if(key&&key->has_release==1&&keyboard.observed_at_ms>=key->last_up_ms)result.number=std::exp(-std::clamp(x,0.0,100.0)*(keyboard.observed_at_ms-key->last_up_ms)/1000.0);break;}
            default:throw RuntimeFault{"invalid_opcode","Unsupported compiled expression"};
        }
        if(e.type==Type::Number)Finite(result.number);return result;
    }
    static bool Zone(const KeyInfo& info,const std::string& zone) {
        const auto& name=info.key_name;
        if(zone.empty()||zone=="all")return true;
        if(zone=="alpha")return name.size()==1&&name[0]>='A'&&name[0]<='Z';
        if(zone=="wasd")return In(name,{"W","A","S","D"});
        if(zone=="arrows")return In(name,{"UP","DOWN","LEFT","RIGHT"});
        if(zone=="numeric")return info.physical_row==1&&info.physical_col>=2&&info.physical_col<=13;
        if(zone=="navigation")return info.physical_col>=14||In(name,{"UP","DOWN","LEFT","RIGHT"});
        return In(name,{"LCTRL","RCTRL","L_CTRL","R_CTRL","LSHIFT","RSHIFT","L_SHIFT","R_SHIFT","LALT","RALT","L_ALT","R_ALT","TAB","CAPS","ENTER"});
    }
    void Pixel(int led,ColorRGB color) {
        if(led<0 || led>=static_cast<int>(TOTAL_LEDS))throw RuntimeFault{"invalid_led","LED index is outside frame bounds"};
        state.frame.SetKey(static_cast<size_t>(led),color);state.coverage.set(static_cast<size_t>(led));
    }
    void ActionNode(const Instruction& n) {
        const auto a=Eval(n.expr[0]);
        if(n.action==Action::Fill){state.frame.Fill(a.color);state.coverage.set();}
        else if(n.action==Action::SetKey){const auto led=Eval(n.expr[1]).number;if(led<0||led>=TOTAL_LEDS)throw RuntimeFault{"invalid_led","LED index outside frame"};Pixel(static_cast<int>(led),a.color);}
        else if(n.action==Action::SetVar)state.variables[n.index]=a.number;
        else if(n.action==Action::ChangeVar){const double next=state.variables[n.index]+a.number;Finite(next);state.variables[n.index]=next;}
        else {
            const double speed=Eval(n.expr[1]).number;double cx=7.5,cy=3;
            auto found=context->keymap.GetAllKeys().find(n.text);if(found!=context->keymap.GetAllKeys().end()){cx=found->second.physical_x;cy=found->second.physical_y;}
            for(uint32_t i=0;i<key_count;++i)if(keys[i]) {
                const auto& info=*keys[i];const double distance=std::hypot(info.physical_x-cx,info.physical_y-cy);
                const double phase=std::fmod(std::fmod(distance-context->elapsed_ms/1000.0*speed,4.0)+4.0,4.0);Finite(phase);
                Pixel(info.led_id,Scale(a.color,std::max(0.0,1-std::fabs(phase-1))));
            }
        }
    }
    bool Step(const Instruction& n) {
        current_key=nullptr; current_key_index=UINT32_MAX;
        if(n.key_slot>=0) {const auto index=state.indices[static_cast<size_t>(n.key_slot)];if(index>=key_count||!keys[index])throw RuntimeFault{"invalid_key_context","Instruction key slot is unavailable"};current_key=keys[index];current_key_index=index;}
        switch(n.kind) {
            case Kind::End:if(program->one_shot){state.terminal=true;state.terminal_at=context->elapsed_ms;}else state.pc=program->entry;return false;
            case Kind::Jump:state.pc=n.next;break;
            case Kind::If:state.pc=Eval(n.expr[0]).boolean?n.yes:n.no;break;
            case Kind::Wait:{const double wait=std::clamp(Eval(n.expr[0]).number,0.0,60000.0);const auto duration=static_cast<uint64_t>(wait);
                state.wake=context->elapsed_ms>UINT64_MAX-duration?UINT64_MAX:context->elapsed_ms+duration;state.pc=n.next;status.waiting=true;return false;}
            case Kind::LoopInit:state.indices[n.slot]=0;state.limits[n.slot]=n.loop==Loop::Keys?key_count:static_cast<uint32_t>(std::clamp(std::floor(Eval(n.expr[0]).number),0.0,10000.0));state.pc=n.next;break;
            case Kind::Increment:if(state.indices[n.slot]<UINT32_MAX)++state.indices[n.slot];state.pc=n.next;break;
            case Kind::LoopTest:
                if(n.loop==Loop::While)state.pc=(Eval(n.expr[0]).boolean!=n.until)?n.yes:n.no;
                else if(n.loop==Loop::Repeat)state.pc=state.indices[n.slot]<state.limits[n.slot]?n.yes:n.no;
                else {const auto index=state.indices[n.slot];if(index>=key_count)state.pc=n.no;
                    else if(keys[index]&&Zone(*keys[index],n.text))state.pc=n.yes;else ++state.indices[n.slot];}
                break;
            case Kind::Action:ActionNode(n);state.pc=n.next;break;
        }
        return true;
    }
    void Run(const EffectContext& ctx,FrameBuffer& output) {
        if(status.faulted){output=state.frame;return;}
        const auto before=state;
        const auto old_status=status;
        status={};context=&ctx;
        try {
            CacheKeys(ctx.keymap);
            if(!state.initialized || ctx.elapsed_ms<state.last){state={};state.initialized=true;state.pc=program->entry;}
            state.last=ctx.elapsed_ms;Keyboard();Hall();
            status.audio_available = ctx.audio && ctx.audio->Enabled();
            status.screen_available = ctx.screen && ctx.screen->Enabled();
            if(state.terminal) {
                if(!program->fade||state.zero_rendered) {state.finished=true;state.opacity=0;}
                else {
                    state.opacity=static_cast<float>(std::clamp(1.0-static_cast<double>(ctx.elapsed_ms-state.terminal_at)/program->fade,0.0,1.0));
                    if(state.opacity==0)state.zero_rendered=true;
                }
            } else if(ctx.elapsed_ms<state.wake) status.waiting=true;
            else {
                while(status.work_units<EffectProgram::FrameBudget) {
                    const auto& node=program->instructions[state.pc];
                    if(node.cost>EffectProgram::FrameBudget-status.work_units){status.yielded=true;break;}
                    status.work_units+=node.cost;++status.instructions;
                    if(!Step(node))break;
                }
                if(status.work_units==EffectProgram::FrameBudget&&!status.waiting&&!state.terminal)status.yielded=true;
            }
            status.pc=state.pc;status.terminal=state.terminal;output=state.frame;
        } catch(const RuntimeFault& error) {
            const auto failed_pc=state.pc;state=before;status=old_status;status.faulted=true;
            status.error={error.code,"/nodes/"+std::to_string(failed_pc),program->instructions[failed_pc].source,error.message};output=state.frame;
        } catch(...) {
            const auto failed_pc=state.pc;state=before;status=old_status;status.faulted=true;
            status.error={"input_failure","/nodes/"+std::to_string(failed_pc),program->instructions[failed_pc].source,"Input callback failed; previous frame/state retained"};output=state.frame;
        }
    }
};

ProgramEffect::ProgramEffect(std::shared_ptr<const EffectProgram> program,
                             const AuraLogicalKeyboardHostV1* keyboard,
                             const AuraHallAnalogHostV1* hall)
    :runtime_(std::make_unique<Runtime>()),program_(program) {
    if(!program)throw std::invalid_argument("ProgramEffect needs compiled program");
    runtime_->program=program->impl_;
    if(keyboard) {
        if(keyboard->struct_size!=sizeof(*keyboard)||keyboard->version!=AURA_LOGICAL_KEYBOARD_VERSION||!keyboard->read_snapshot)
            throw std::invalid_argument("Unsupported keyboard callback contract");
        runtime_->host=*keyboard;
    }
    if(hall) {
        if(hall->struct_size!=sizeof(*hall)||hall->version!=AURA_HALL_ANALOG_VERSION||!hall->read_snapshot)
            throw std::invalid_argument("Unsupported Hall analog callback contract");
        runtime_->hall_host=*hall;
    }
}
ProgramEffect::~ProgramEffect()=default;
void ProgramEffect::Render(uint64_t time,FrameBuffer& output,const Keymap& keymap){RenderWithContext(EffectContext(time,keymap),output);}
void ProgramEffect::RenderWithContext(const EffectContext& context,FrameBuffer& output){runtime_->Run(context,output);}
void ProgramEffect::Reset(){runtime_->state={};runtime_->status={};}
bool ProgramEffect::Finished()const{return runtime_->state.finished;}
float ProgramEffect::Opacity()const{return runtime_->state.opacity;}
const std::bitset<TOTAL_LEDS>& ProgramEffect::Coverage()const{return runtime_->state.coverage;}
const EffectProgramStatus& ProgramEffect::Status()const{return runtime_->status;}
uint64_t ProgramEffect::GenerationId()const{return runtime_->generation_id;}
void ProgramEffect::SetGenerationId(uint64_t generation_id){runtime_->generation_id=generation_id;}
} // namespace aura
