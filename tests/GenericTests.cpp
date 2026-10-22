#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

struct Checked {
    nus::SourceManager sources;
    nus::ast::SourceFile ast;
    nus::sema::SemanticAnalyzer semantic;
};

Checked check(std::string_view source) {
    Checked result;
    const auto id = result.sources.addSource("<generic-test>", std::string(source));
    nus::Lexer lexer(result.sources, id);
    nus::Parser parser(result.sources, lexer.tokenize());
    result.ast = parser.parseSourceFile();
    if (parser.hasErrors()) {
        for (const auto& diagnostic : parser.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("parser rejected generic test");
    }
    result.semantic.analyze(result.ast);
    return result;
}

bool hasDiagnostic(const nus::sema::SemanticAnalyzer& semantic, std::string_view text) {
    for (const auto& diagnostic : semantic.diagnostics()) {
        if (diagnostic.message.find(text) != std::string::npos) return true;
    }
    return false;
}

void testMonomorphization() {
    auto checked = check(R"(
        fn identity<T>(value: T) -> T { value }
        fn main() {
            let a = identity(42);
            let b = identity(true);
            print(a, b);
        }
    )");
    if (checked.semantic.hasErrors()) fail("valid generic program was rejected");
    const auto hir = nus::hir::HirBuilder(checked.semantic).lower(checked.ast);
    bool i32 = false;
    bool boolean = false;
    for (const auto& function : hir.functions) {
        i32 = i32 || function.name == "identity$g$i32";
        boolean = boolean || function.name == "identity$g$bool";
    }
    if (!i32 || !boolean) fail("generic function was not monomorphized for each concrete type");
}

void testCopyBound() {
    auto valid = check(R"(
        fn duplicate<T: Copy>(value: T) -> T { value }
        fn main() { print(duplicate(10)); }
    )");
    if (valid.semantic.hasErrors()) fail("Copy-bound generic rejected i32");

    auto invalid = check(R"(
        struct Packet { value: i32, }
        fn duplicate<T: Copy>(value: T) -> T { value }
        fn main() {
            let packet = Packet { value: 1 };
            let other = duplicate(packet);
        }
    )");
    if (!hasDiagnostic(invalid.semantic, "does not satisfy trait bound `Copy`")) {
        fail("Copy bound did not reject non-Copy struct");
    }
}

void testMarkerTrait() {
    auto checked = check(R"(
        trait NetworkValue {}
        struct Packet { value: i32, }
        impl NetworkValue for Packet {}
        fn accept<T: NetworkValue>(value: T) -> i32 { 1 }
        fn main() {
            let packet = Packet { value: 5 };
            print(accept(packet));
        }
    )");
    if (checked.semantic.hasErrors()) fail("marker trait implementation did not satisfy bound");
}

void testGenericRuntimeTypes() {
    auto checked = check(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            vec_push(&mut values, 10);
            let first: i32 = vec_get(&values, 0);
            let length: usize = vec_len(&values);

            let some_value: Option<i32> = some(42);
            let no_value: Option<i32> = none::<i32>();
            let ok_value: Result<i32, string> = ok::<i32, string>(7);
            let err_value: Result<i32, string> = err::<i32, string>("error");
        }
    )");
    if (checked.semantic.hasErrors()) {
        for (const auto& diagnostic : checked.semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("generic runtime types were rejected");
    }
}
}

int main() {
    testMonomorphization();
    testCopyBound();
    testMarkerTrait();
    testGenericRuntimeTypes();
    std::cout << "generic tests passed\n";
    return 0;
}
