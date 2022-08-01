#include "lalrbld.h"
#include "parser.h"

#include "uxs/io/filebuf.h"

template<typename Iter>
void outputData(uxs::iobuf& outp, Iter from, Iter to, size_t ntab = 0) {
    auto convert_to_string = [](const auto& v) {
        if constexpr (std::is_constructible<std::string, decltype(v)>::value) {
            return '\"' + v + '\"';
        } else {
            return uxs::to_string(v);
        }
    };

    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + convert_to_string(*from);
    while (++from != to) {
        auto sval = convert_to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            outp.write(line).put(',').endl();
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    outp.write(line).endl();
}

template<typename Iter>
void outputArray(uxs::iobuf& outp, std::string_view array_name, Iter from, Iter to) {
    if (from == to) { return; }
    if constexpr (std::is_constructible<std::string_view, decltype(*from)>::value) {
        outp.endl().write("static const char* ");
    } else {
        outp.endl().write("static int ");
    }
    uxs::fprintln(outp, "{}[{}] = {{", array_name, std::distance(from, to));
    outputData(outp, from, to, 4);
    outp.write("};").endl();
}

void outputParserEngine(uxs::iobuf& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "int parse(int tt, std::vector<int>& state_stack, unsigned& rlen, int rise_error) {",
        "    enum { kShiftFlag = 1, kFlagCount = 1 };",
        "    int action = rise_error;",
        "    if (action >= 0) {",
        "        const int* action_tbl = &action_list[action_idx[state_stack.back()]];",
        "        while (action_tbl[0] >= 0 && action_tbl[0] != tt) { action_tbl += 2; }",
        "        action = action_tbl[1];",
        "    }",
        "    if (action < 0) {  // Roll back to state, which can accept error",
        "        rlen = 0;",
        "        do {",
        "            const int* action_tbl = &action_list[action_idx[state_stack.back()]];",
        "            while (action_tbl[0] >= 0 && action_tbl[0] != predef_tt_error) { action_tbl += 2; }",
        "            if (action_tbl[1] >= 0 && (action_tbl[1] & kShiftFlag)) {  // Can recover",
        "                state_stack.push_back(action_tbl[1] >> kFlagCount);    // Shift error token",
        "                break;",
        "            }",
        "            state_stack.pop_back();",
        "            ++rlen;",
        "        } while (!state_stack.empty());",
        "        return action;",
        "    } else if (action & kShiftFlag) {",
        "        state_stack.push_back(action >> kFlagCount);",
        "        return predef_act_shift;",
        "    }",
        "    const int* info = &reduce_info[action >> kFlagCount];",
        "    rlen = static_cast<unsigned>(info[0]);",
        "    state_stack.erase(state_stack.end() - rlen, state_stack.end());",
        "    int state = state_stack.back();",
        "    const int* goto_tbl = &goto_list[info[1]];",
        "    while (goto_tbl[0] >= 0 && goto_tbl[0] != state) { goto_tbl += 2; }",
        "    state_stack.push_back(goto_tbl[1]);",
        "    return predef_act_reduce + info[2];",
        "}",
    };
    // clang-format on
    outp.endl();
    for (const auto& l : text) { outp.write(l).endl(); }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        std::string input_file_name;
        std::string analyzer_file_name("parser_analyzer.inl");
        std::string defs_file_name("parser_defs.h");
        std::string report_file_name;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "-o") {
                if (++i < argc) { analyzer_file_name = argv[i]; }
            } else if (arg == "-h") {
                if (++i < argc) { defs_file_name = argv[i]; }
            } else if (arg == "--report") {
                if (++i < argc) { report_file_name = argv[i]; }
            } else if (arg == "--help") {
                // clang-format off
                static constexpr std::string_view text[] = {
                    "Usage: parsegen [options] file",
                    "Options:",
                    "    -o <file>           Place the output analyzer into <file>.",
                    "    -h <file>           Place the output definitions into <file>.",
                    "    --report <file>     Place analyzer build report into <file>.",
                    "    --help              Display this information.",
                };
                // clang-format on
                for (const auto& l : text) { uxs::stdbuf::out.write(l).endl(); }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = arg;
            } else {
                logger::fatal().format("unknown flag `{}`", arg);
                return -1;
            }
        }

        if (input_file_name.empty()) {
            logger::fatal().format("no input file specified");
            return -1;
        }

        uxs::filebuf ifile(input_file_name.c_str(), "rt");
        if (!ifile) {
            logger::fatal().format("could not open input file `{}`", input_file_name);
            return -1;
        }

        Grammar grammar;
        Parser parser(ifile, input_file_name, grammar);
        if (!parser.parse()) { return -1; }

        LRBuilder lr_builder(grammar);
        lr_builder.buildAnalizer();

        if (unsigned sr_count = lr_builder.getSRConflictCount(); sr_count > 0) {
            logger::warning(input_file_name).format("{} shift/reduce conflict(s) found", sr_count);
        }
        if (unsigned rr_count = lr_builder.getRRConflictCount(); rr_count > 0) {
            logger::warning(input_file_name).format("{} reduce/reduce conflict(s) found", rr_count);
        }

        if (!report_file_name.empty()) {
            if (uxs::filebuf ofile(report_file_name.c_str(), "wt"); ofile) {
                grammar.printTokens(ofile);
                grammar.printNonterms(ofile);
                grammar.printActions(ofile);
                grammar.printGrammar(ofile);
                lr_builder.printFirstTable(ofile);
                lr_builder.printAetaTable(ofile);
                lr_builder.printStates(ofile);
            } else {
                logger::error().format("could not open report file `{}`", report_file_name);
            }
        }

        if (uxs::filebuf ofile(defs_file_name.c_str(), "wt"); ofile) {
            ofile.write("// Parsegen autogenerated definition file - do not edit!").endl();
            ofile.endl().write("enum {").endl();
            uxs::fprintln(ofile, "    predef_tt_error = {},", static_cast<int>(kTokenError));
            unsigned last_tt_id = kTokenError;
            for (const auto& [name, id] : grammar.getTokenList()) {
                uxs::fprint(ofile, "    tt_{}", name);
                if (id > last_tt_id + 1) { uxs::fprint(ofile, " = {}", id); }
                ofile.put(',').endl();
                last_tt_id = id;
            }
            ofile.write("    total_token_count,").endl();
            ofile.write("};").endl();

            ofile.endl().write("enum {").endl();
            ofile.write("    predef_act_shift = 0,").endl();
            ofile.write("    predef_act_reduce = 1,").endl();
            unsigned last_act_id = 0;
            for (const auto& [name, id] : grammar.getActionList()) {
                uxs::fprint(ofile, "    act_{}", name);
                if (id != last_act_id + 1) { uxs::fprint(ofile, " = {}", id + 1); }
                ofile.put(',').endl();
                last_act_id = id;
            }
            ofile.write("    total_action_count,").endl();
            ofile.write("};").endl();

            if (const auto& start_conditions = grammar.getStartConditions(); !start_conditions.empty()) {
                ofile.endl().write("enum {").endl();
                uxs::fprintln(ofile, "    sc_{} = 0,", start_conditions[0].first);
                for (size_t i = 1; i < start_conditions.size(); ++i) {
                    uxs::fprintln(ofile, "    sc_{},", start_conditions[i].first);
                }
                ofile.write("};").endl();
            }

            ofile.endl();
            ofile.write("int parse(int tt, std::vector<int>& state_stack, unsigned& rlen, int rise_error);");
            ofile.endl();
        } else {
            logger::error().format("could not open output file `{}`", defs_file_name);
        }

        const auto& action_table = lr_builder.getCompressedActionTable();
        std::vector<int> action_idx(action_table.index.size()), action_list;
        action_list.reserve(2 * action_table.data.size());
        auto action_code = [](const LRBuilder::Action& action) {
            enum { kShiftFlag = 1, kFlagCount = 1 };
            switch (action.type) {
                case LRBuilder::Action::Type::kShift: return static_cast<int>(action.val << kFlagCount) | kShiftFlag;
                case LRBuilder::Action::Type::kReduce: return static_cast<int>(3 * action.val) << kFlagCount;
                default: break;
            }
            return -1;
        };
        std::transform(action_table.index.begin(), action_table.index.end(), action_idx.begin(),
                       [](unsigned i) { return 2 * i; });
        for (const auto& [n_state, action] : action_table.data) {
            action_list.push_back(n_state);
            action_list.push_back(action_code(action));
        }

        const auto& goto_table = lr_builder.getCompressedGotoTable();
        std::vector<int> goto_list;
        goto_list.reserve(2 * goto_table.data.size());
        for (const auto& [n_nonterm, n_new_state] : goto_table.data) {
            goto_list.push_back(n_nonterm);
            goto_list.push_back(n_new_state);
        }

        if (uxs::filebuf ofile(analyzer_file_name.c_str(), "wt"); ofile) {
            ofile.write("// Parsegen autogenerated analyzer file - do not edit!").endl();
            outputArray(ofile, "action_idx", action_idx.begin(), action_idx.end());
            outputArray(ofile, "action_list", action_list.begin(), action_list.end());

            std::vector<int> reduce_info;
            reduce_info.reserve(3 * grammar.getProductionCount());
            for (unsigned n_prod = 0; n_prod < grammar.getProductionCount(); ++n_prod) {
                const auto& prod = grammar.getProductionInfo(n_prod);
                reduce_info.push_back(static_cast<int>(prod.rhs.size()));         // Length
                reduce_info.push_back(2 * goto_table.index[getIndex(prod.lhs)]);  // Goto index
                reduce_info.push_back(prod.action);                               // Action on reduce
            }

            outputArray(ofile, "reduce_info", reduce_info.begin(), reduce_info.end());
            outputArray(ofile, "goto_list", goto_list.begin(), goto_list.end());
            outputParserEngine(ofile);
        } else {
            logger::error().format("could not open output file `{}`", analyzer_file_name);
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal().format("exception catched: {}", e.what()); }
    return -1;
}
