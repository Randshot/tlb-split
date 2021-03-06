#include <ioctl.h>
#include <guard_exceptions.h>
#include <bitmanip.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <limits.h>
#include <bitset>

using int_t = uintptr_t;
using ptr_t = void*;

struct flip_data {
    int_t rip = 0;
    int_t gva = 0;
    int_t orig_gva = 0;
    int_t gpa = 0;
    int_t d_pa = 0;
    int_t cr3 = 0;
    int_t bits = 0;
    int_t counter = 0;

    flip_data() = default;
    flip_data(int_t _rip, int_t _gva, int_t _orig_gva, int_t _gpa, int_t _d_pa, int_t _cr3, int_t _bits, int_t _counter)
    {
        rip = _rip;
        gva = _gva;
        orig_gva = _orig_gva;
        gpa = _gpa;
        d_pa = _d_pa;
        cr3 = _cr3;
        bits = _bits;
        counter = _counter;
    }

    ~flip_data() = default;
};

namespace access_t
{
    constexpr const auto read = 0;
    constexpr const auto write = 1;
    constexpr const auto exec = 2;
}

std::vector<flip_data> g_flip_log;

/*
void
hello_world()
{ std::cout << "hello world" << std::endl << std::endl; }

void
hooked_hello_world()
{ std::cout << "hooked hello world" << std::endl << std::endl; }

ptr_t
view_as_pointer(int_t addr)
{ return reinterpret_cast<ptr_t>(addr); }
*/

namespace detail {
    constexpr int HEX_DIGIT_BITS = 4;
    constexpr int HEX_BASE_CHARS = 2; // Optional.  See footnote #2.

    // Replaced CharCheck with a much simpler trait.
    template<typename T> struct is_char
      : std::integral_constant<bool,
        std::is_same<T, char>::value ||
        std::is_same<T, signed char>::value ||
        std::is_same<T, unsigned char>::value> {};
}

template<typename T>
std::string hex_out_s(T val, int width = (sizeof(T) * CHAR_BIT / detail::HEX_DIGIT_BITS)) {
    using namespace detail;

    std::stringstream sformatter;
    sformatter << std::hex
               << std::internal
               << std::showbase
               << std::setfill('0')
               << std::setw(width + HEX_BASE_CHARS)
               << (is_char<T>::value ? static_cast<unsigned int>(val) : val);

    return sformatter.str();
}

const int_t ida_base = 0x140000000ull;

template<typename T>
auto
transl(T addr, const int_t base)
{
    return addr - base + ida_base;
}

int
main(int argc, const char *argv[])
{
    vmcall_registers_t regs;

    guard_exceptions([&]
    {
        // Open IOCTL connection.
        ioctl ctl;
        ctl.open();

        /// <r00> [RESERVED] vmcall mode (2)
        /// <r01> [RESERVED] magic number (0xB045EACDACD52E22)
        ///
        /// <r02> Method switch table
        ///
        /// 0 = hv_present()
        /// 1 = create_split_context(int_t gva)
        /// 2 = activate_split(int_t gva)
        /// 3 = deactivate_split(int_t gva)
        /// 4 = deactivate_all_splits()
        /// 5 = is_split(int_t gva)
        /// 6 = write_to_c_page(int_t from_va, int_t to_va, size_t size)
        /// 7 = get_flip_num()
        /// 8 = get_flip_data(int_t out_addr, int_t out_size)
        /// 9 = clear_flip_data()
        /// 10 = remove_flip_entry(int_t rip)
        ///
        /// <r03+> for args
        ///

        int_t module_base = ida_base;
        if (argc == 2)
        {
            std::string cmd{ argv[1] };

            if (cmd == "--clear" || cmd == "-c")
            {
                // VMCALL: Clear flip data log.
                regs.r00 = VMCALL_REGISTERS;
                regs.r01 = VMCALL_MAGIC_NUMBER;
                regs.r02 = 9;
                ctl.call_ioctl_vmcall(&regs, 0);
                std::cout << "flip data cleared" << std::endl;
                exit(0);
            }
            else if (cmd == "--help" || cmd == "-h")
            {
                std::cout << "Usage: hook.exe [OPTION] <arg>" << std::endl
                    << "  --help, -h: Display this help message" << std::endl
                    << "  --clear, -c: Clear flip data log" << std::endl
                    << "  --remove, -r <addr>: Remove all entries with give address from flip data log" << std::endl
                    << "  --deall, -a: Deatcivate all splits " << std::endl
                    << "  <addr>: Given address will be used as module base to normalize the data" << std::endl
                    << std::endl
                    ;
                exit(0);
            }
            else if (cmd == "--deall" || cmd == "-a")
            {
                // VMCALL: Deactivate all splits
                regs.r00 = VMCALL_REGISTERS;
                regs.r01 = VMCALL_MAGIC_NUMBER;
                regs.r02 = 4;
                ctl.call_ioctl_vmcall(&regs, 0);
                std::cout << "all splits deactivated" << std::endl;
                exit(0);
            }
            else
            {
                module_base = std::stoull(cmd, 0, 16);
                std::cout << "Module Base: " << hex_out_s(module_base) << std::endl;
            }
        }
        else if (argc == 3)
        {
            std::string cmd{ argv[1] };
            std::string val{ argv[2] };

            if (cmd == "--remove" || cmd == "-r")
            {
                auto &&addr = std::stoull(val, 0, 16);
                // VMCALL: Clear flip data log.
                regs.r00 = VMCALL_REGISTERS;
                regs.r01 = VMCALL_MAGIC_NUMBER;
                regs.r02 = 10;
                regs.r03 = addr;
                ctl.call_ioctl_vmcall(&regs, 0);
                std::cout << "removed " << hex_out_s(addr) << " from flip data log" << std::endl;
                exit(0);
            }
            else
            {
                std::cout << "unknown command" << std::endl;
                exit(0);
            }
        }

        // VMCALL: Check if an hv is present.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 0;
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "hv_present: " << (regs.r02 == 1 ? "yes" : "no") << std::endl;

        /*
        // VMCALL: Check if page is split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 5;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "is_split: " << (regs.r02 == 1 ? "yes" : "no") << std::endl;

        std::cout << "before create_split_context: ";
        hello_world();

        // VMCALL: Create new split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 1;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "create_split_context: " << (regs.r02 == 1 ? "success" : "failure") << std::endl;

        std::cout << "after create_split_context: ";
        hello_world();

        // VMCALL: Check if page is split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 5;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "is_split: " << (regs.r02 == 1 ? "yes" : "no") << std::endl;

        // VMCALL: Activate split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 2;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "activate_split: " << (regs.r02 == 1 ? "success" : "failure") << std::endl;

        std::cout << "after activate_split: ";
        unsigned char* v = new unsigned char[8];
        std::memmove(v, reinterpret_cast<void*>(hello_world), 8);
        delete v;
        hello_world();

        // VMCALL: Check if page is split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 5;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "is_split: " << (regs.r02 == 1 ? "yes" : "no") << std::endl;

        // VMCALL: Deactivate split.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 3;
        regs.r03 = reinterpret_cast<uintptr_t>(hello_world);
        ctl.call_ioctl_vmcall(&regs, 0);
        std::cout << "deactivate_split: " << (regs.r02 == 1 ? "success" : "failure") << std::endl;

        std::cout << "after deactivate_split: ";
        hello_world();
        */

        // VMCALL: Get data num.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 7;
        ctl.call_ioctl_vmcall(&regs, 0);
        auto data_num = regs.r02;

        if (data_num == 0)
        {
            std::cout << "no flip data" << std::endl;
            exit(0);
        }
        else
            std::cout << "# of registered flips: " << data_num << std::endl;

        // Reserve enough space.
        std::vector<flip_data> local_flip_log;
        local_flip_log.resize(data_num);

        // VMCALL: Get latest flip data.
        regs.r00 = VMCALL_REGISTERS;
        regs.r01 = VMCALL_MAGIC_NUMBER;
        regs.r02 = 8;
        regs.r03 = reinterpret_cast<int_t>(local_flip_log.data());
        regs.r04 = data_num * sizeof(flip_data);
        ctl.call_ioctl_vmcall(&regs, 0);

        // Sort the flip data log by RIP and counter (ascending).
        std::sort(local_flip_log.begin(), local_flip_log.end(), [](const flip_data& a, const flip_data& b)
        {
            return (a.rip < b.rip) || (a.rip == b.rip && a.counter < b.counter);
        });

        for (const auto & flip : local_flip_log)
        {
            // Filter out execute only flips.
            if (is_bit_set(flip.bits, access_t::exec))
                continue;

            std::cout
              << "["
              << (is_bit_set(flip.bits, access_t::read)     ? "R" : "-")
              << (is_bit_set(flip.bits, access_t::write)    ? "W" : "-")
              << (is_bit_set(flip.bits, access_t::exec)     ? "X" : "-")
              << "]:"
              << " rip: " << hex_out_s(transl(flip.rip, module_base))
              << " gva: " << hex_out_s(transl(flip.gva, module_base))
              << " orig_gva: " << hex_out_s(transl(flip.orig_gva, module_base))
              //<< " gpa: " << hex_out_s(flip.gpa)
              //<< " d_pa: " << hex_out_s(flip.d_pa)
              << " cr3: " << hex_out_s(flip.cr3, 8)
              << " counter: " << flip.counter
              //<< " bits: " << std::bitset<3>(access_bits)
              << std::endl;
        }

    });

    return 0;
}
