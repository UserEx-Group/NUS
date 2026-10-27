#include "nus/codegen/Layout.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
}

int main() {
    using nus::sema::Type;
    using nus::sema::TypeKind;

    nus::mir::Program program;
    nus::mir::Struct packet;
    packet.name = "Packet";
    packet.fields = {
        {"source", Type::simple(TypeKind::U32)},
        {"destination", Type::simple(TypeKind::U32)},
        {"payload_size", Type::simple(TypeKind::USize)},
    };
    program.structs.push_back(packet);

    nus::codegen::TargetInfo target;
    target.pointer_bits = 64;
    target.triple = "x86_64-test";
    nus::codegen::LayoutEngine layouts(program, target);

    const auto string_layout = layouts.layoutOf(Type::simple(TypeKind::String));
    if (string_layout.size != 24 || string_layout.alignment != 8) fail("64-bit string ABI must be three words");

    const auto vector_layout = layouts.layoutOf(Type::vector(Type::simple(TypeKind::I32)));
    if (vector_layout.size != 24 || vector_layout.alignment != 8) fail("64-bit Vec ABI must be three words");

    const auto iterator_layout = layouts.layoutOf(Type::vectorIterator(Type::simple(TypeKind::I32)));
    if (iterator_layout.size != 32 || iterator_layout.alignment != 8) fail("64-bit VecIter ABI must be four words");

    const auto ref_iterator_layout = layouts.layoutOf(Type::borrowedVectorIterator(Type::simple(TypeKind::I32), false));
    if (ref_iterator_layout.size != 24 || ref_iterator_layout.alignment != 8) fail("64-bit VecRefIter ABI must be three words");
    const auto mut_iterator_layout = layouts.layoutOf(Type::borrowedVectorIterator(Type::simple(TypeKind::I32), true));
    if (mut_iterator_layout.size != 24 || mut_iterator_layout.alignment != 8) fail("64-bit VecMutIter ABI must be three words");

    const auto ip_layout = layouts.layoutOf(Type::simple(TypeKind::IpAddr));
    if (ip_layout.size != 4 || ip_layout.alignment != 4) fail("IpAddr ABI mismatch");
    const auto socket_addr_layout = layouts.layoutOf(Type::simple(TypeKind::SocketAddr));
    if (socket_addr_layout.size != 8 || socket_addr_layout.alignment != 4) fail("SocketAddr ABI mismatch");
    const auto error_layout = layouts.layoutOf(Type::simple(TypeKind::NetworkError));
    if (error_layout.size != 8 || error_layout.alignment != 4) fail("NetworkError ABI mismatch");
    const auto tcp_layout = layouts.layoutOf(Type::simple(TypeKind::TcpStream));
    if (tcp_layout.size != 8 || tcp_layout.alignment != 8) fail("TcpStream ABI mismatch");
    const auto listener_layout = layouts.layoutOf(Type::simple(TypeKind::TcpListener));
    if (listener_layout.size != 8 || listener_layout.alignment != 8) fail("TcpListener ABI mismatch");
    const auto udp_layout = layouts.layoutOf(Type::simple(TypeKind::UdpSocket));
    if (udp_layout.size != 8 || udp_layout.alignment != 8) fail("UdpSocket ABI mismatch");

    const auto packet_layout = layouts.layoutOfStruct("Packet");
    if (packet_layout.layout.size != 16 || packet_layout.layout.alignment != 8) fail("Packet layout mismatch");
    if (packet_layout.fields.size() != 3 || packet_layout.fields[0].offset != 0 ||
        packet_layout.fields[1].offset != 4 || packet_layout.fields[2].offset != 8) {
        fail("Packet field offsets mismatch");
    }

    std::cout << "layout tests passed\n";
    return 0;
}
