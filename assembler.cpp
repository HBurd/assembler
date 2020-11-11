#include <fstream>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include <sys/stat.h>  // TODO: Portability (unix only)

template <typename T, uint32_t max_size>
struct Array
{
    uint32_t size = 0;
    T data[max_size];
    void push(T element)
    {
        assert(size < max_size);
        data[size++] = element;
    }

    T pop()
    {
        assert(size);
        return data[--size];
    }

    T& operator[](uint32_t idx)
    {
        return data[idx];
    }
};

// this is the bootloader ROM size
constexpr uint32_t ROM_SIZE = 1024;
constexpr uint32_t MAX_INSTR = ROM_SIZE / 2;

// these are all basically arbitrary
constexpr uint32_t LINE_BUF_SIZE = 256;
constexpr uint32_t LABEL_BUF_SIZE = 65536;
constexpr uint32_t MAX_LABELS = 512;

uint32_t line_num = 0;      // hack for line numbers in error messages

using std::ifstream;
using std::cout;
using std::endl;

constexpr uint32_t INVALID_OP = 0xFFFFFFFF;

enum class InstructionFormat
{
    A0,
    A1,
    A2,
    A3,
    B1,
    B2,
    L1,
    L2,
};

struct OpData
{
    const char* mnemonic;
    uint32_t opcode;
    InstructionFormat format;
    bool upper; // only for loadimm.upper
} OP[] = {
    {.mnemonic = "NOP",           .opcode = 0,  .format = InstructionFormat::A0, .upper = false},
    {.mnemonic = "ADD",           .opcode = 1,  .format = InstructionFormat::A1, .upper = false},
    {.mnemonic = "SUB",           .opcode = 2,  .format = InstructionFormat::A1, .upper = false},
    {.mnemonic = "MUL",           .opcode = 3,  .format = InstructionFormat::A1, .upper = false},
    {.mnemonic = "NAND",          .opcode = 4,  .format = InstructionFormat::A1, .upper = false},
    {.mnemonic = "SHL",           .opcode = 5,  .format = InstructionFormat::A2, .upper = false},
    {.mnemonic = "SHR",           .opcode = 6,  .format = InstructionFormat::A2, .upper = false},
    {.mnemonic = "TEST",          .opcode = 7,  .format = InstructionFormat::A3, .upper = false},
    {.mnemonic = "MUH",           .opcode = 8,  .format = InstructionFormat::A1, .upper = false},
    {.mnemonic = "OUT",           .opcode = 32, .format = InstructionFormat::A3, .upper = false},
    {.mnemonic = "IN",            .opcode = 33, .format = InstructionFormat::A3, .upper = false},
    {.mnemonic = "BRR",           .opcode = 64, .format = InstructionFormat::B1, .upper = false},
    {.mnemonic = "BRR.N",         .opcode = 65, .format = InstructionFormat::B1, .upper = false},
    {.mnemonic = "BRR.Z",         .opcode = 66, .format = InstructionFormat::B1, .upper = false},
    {.mnemonic = "BRR.O",         .opcode = 73, .format = InstructionFormat::B1, .upper = false},
    {.mnemonic = "BR",            .opcode = 67, .format = InstructionFormat::B2, .upper = false},
    {.mnemonic = "BR.N",          .opcode = 68, .format = InstructionFormat::B2, .upper = false},
    {.mnemonic = "BR.Z",          .opcode = 69, .format = InstructionFormat::B2, .upper = false},
    {.mnemonic = "BR.O",          .opcode = 72, .format = InstructionFormat::B2, .upper = false},
    {.mnemonic = "BR.SUB",        .opcode = 70, .format = InstructionFormat::B2, .upper = false},
    {.mnemonic = "RETURN",        .opcode = 71, .format = InstructionFormat::A0, .upper = false},
    {.mnemonic = "LOAD",          .opcode = 16, .format = InstructionFormat::L2, .upper = false},
    {.mnemonic = "STORE",         .opcode = 17, .format = InstructionFormat::L2, .upper = false},
    {.mnemonic = "LOADIMM.LOWER", .opcode = 18, .format = InstructionFormat::L1, .upper = false},
    {.mnemonic = "LOADIMM.UPPER", .opcode = 18, .format = InstructionFormat::L1, .upper = true},
    {.mnemonic = "MOV",           .opcode = 19, .format = InstructionFormat::L2, .upper = false}
};

constexpr uint32_t NUM_OPCODES = sizeof(OP) / sizeof(*OP);
     

struct SubString
{
    uint32_t len = 0;
    char* data = {};

    bool operator==(const char* rhs)
    {
        uint32_t i = 0;
        for (; i < len; ++i)
        {
            if (data[i] != rhs[i]) return false;
        }
        return !rhs[i]; // length check
    }

    bool operator!=(const char* rhs)
    {
        return !((*this) == rhs);
    }

    bool operator==(const SubString& rhs)
    {
        if (rhs.len != len) return false;

        for (uint32_t i = 0; i < len; ++i)
        {
            if (data[i] != rhs[i]) return false;
        }
        return true; // length check
    }

    bool operator!=(const SubString& rhs)
    {
        return !((*this) == rhs);
    }

    char& operator[](size_t index)
    {
        return data[index];
    }

    const char& operator[](size_t index) const
    {
        return data[index];
    }

    void print()
    {
        for (uint32_t i = 0; i < len; ++i)
        {
            cout << data[i];
        }
        cout << endl;
    }
};

struct Label
{
    SubString name;
    uint32_t addr;
};

void to_upper(char* buf)
{
    uint32_t i = 0;
    while (buf[i])
    {
        buf[i] = toupper(buf[i]);
        ++i;
    }
}

void strip_comments(char* buf)
{
    uint32_t i = 0;
    bool found_comment = false;
    while (buf[i])
    {
        if (buf[i] == ';')
        {
            found_comment = true;
        }
        if (found_comment)
        {
            buf[i] = 0;
        }
        ++i;
    }
}

// skips to start of next "word" (newline counts as a word)
void skip_blank(char** buf)
{
    uint32_t i = 0;
    while((*buf)[i])
    {
        if ((*buf)[i] == '.'
            || (*buf)[i] == ':'
            || ((*buf)[i] >= 'A' && (*buf)[i] <= 'Z')
            || ((*buf)[i] >= '0' && (*buf)[i] <= '9')
            || ((*buf)[i] == '\n'))
        {
            break;
        }
        ++i;
    }
    *buf = *buf + i;
}

void skip_word(char** buf)
{
    // newline is a valid one-char word, can't be part of any other
    if ((*buf)[0] == '\n')
    {
        *buf += 1;
        return;
    }

    uint32_t i = 0;
    while((*buf)[i])
    {
        if (!((*buf)[i] == '.'
            || (*buf)[i] == ':'
            || ((*buf)[i] >= 'A' && (*buf)[i] <= 'Z')
            || ((*buf)[i] >= '0' && (*buf)[i] <= '9')))
        {
            break;
        }
        ++i;
    }
    *buf = *buf + i;
}

SubString get_word(char** buf)
{
    SubString result = {};

    // go to start of next word
    skip_blank(buf);
    result.data = *buf;
    cout << result.data[0] << endl;

    // go to end of word;
    skip_word(buf);
    result.len = *buf - result.data;
    
    return result;
}

uint32_t lookup_op(SubString word)
{
    for (uint32_t i = 0; i < NUM_OPCODES; ++i)
    {
        if (word == OP[i].mnemonic) return i;
    }
    return INVALID_OP;
}

bool is_label(SubString word)
{
    if (word.len == 0) return false;
    return word[word.len - 1] == ':';
}

bool is_comment(SubString word)
{
    if (word.len == 0) return false;
    return word[0] == ';';
}

void asm_assert(bool condition, const char* err_msg)
{
    if (!condition)
    {
        cout << "Line " << line_num << ": " << err_msg << endl;
        exit(1);
    }
}

// if success is null, this function can fail and output its own error message
// otherwise it's the caller's problem
int32_t parse_num(SubString num_str, uint32_t bits, bool* success)
{
    // check if signed
    bool negative = false;
    if (num_str[0] == '+' || num_str[0] == '-')
    {
        if (num_str[0] == '-')
        {
            negative = true;
        }
        num_str.data += 1;
        num_str.len -= 1;
    }

    uint32_t base = 10;
    // check base
    if (num_str.len >= 2)
    {
        if (num_str[1] == 'X')
        {
            asm_assert(num_str[0] == '0', "malformed constant");
            num_str.data += 2;
            num_str.len -= 2;
            base = 16;
        }
        else if (num_str[1] == 'B')
        {
            asm_assert(num_str[0] == '0', "malformed constant");
            num_str.data += 2;
            num_str.len -= 2;
            base = 2;
        }
    }

    bool valid_number = true;
    // check it's a valid number so we know if zero is correct
    for (uint32_t i = 0; i < num_str.len; ++i)
    {
        if (base == 10)
        {
            valid_number = num_str[i] >= '0' && num_str[i] <= '9';
        }
        else if (base == 16)
        {
            valid_number = (num_str[i] >= '0' && num_str[i] <= '9') || (num_str[i] >= 'A' && num_str[i] <= 'F');
        }
        else if (base == 2)
        {
            valid_number = (num_str[i] == '0' || num_str[i] == '1');
        }

        if (!valid_number) break;
    }

    if (!success) asm_assert(valid_number, "malformed constant");
    else *success = valid_number;

    int32_t result = (int32_t)strtoul(std::string(num_str.data, num_str.len).c_str(), nullptr, base);
    if (negative) result = -result;

    asm_assert((result & (0xffffffff << bits)) == 0
        || (result & (0xffffffff << bits)) == (0xffffffff << bits), "argument needs too many bits");

    // mask to only the bits we care about
    result &= ~(0xffffffff << bits);
    return result;
}

int32_t parse_constant(SubString const_str, uint32_t addr, Array<Label, MAX_LABELS> labels, uint32_t bits)
{
    bool valid_number;
    int32_t result = parse_num(const_str, bits, &valid_number);
    if (!valid_number)
    {
        bool label_found = false;
        // check if it's a label
        for (uint32_t i = 0; i < labels.size; ++i)
        {
            if (const_str == labels[i].name)
            {
                // it's a label
                label_found = true;

                result = ((int32_t)labels[i].addr - (int32_t)addr) / 2;

                asm_assert((result & (0xffffffff << bits)) == 0
                    || (result & (0xffffffff << bits)) == (0xffffffff << bits), "argument needs too many bits");

                // mask to only the bits we care about
                result &= ~(0xffffffff << bits);
            }
        }
        asm_assert(label_found, "label not found");
    }

    return result;
}

uint32_t parse_reg(SubString reg_str)
{
    asm_assert(reg_str.len == 2 && reg_str[0] == 'R', "not a valid register");
    return reg_str[1] - '0';
}

uint16_t parse_instruction(SubString op_str, Array<SubString, 3> args, Array<Label, MAX_LABELS> labels, uint32_t addr)
{
    uint16_t instr = 0;

    uint32_t op_idx = lookup_op(op_str);
    assert(op_idx != INVALID_OP);
    OpData op = OP[op_idx];

    instr |= op.opcode << 9;

    switch (op.format)
    {
        case InstructionFormat::A0:
            asm_assert(args.size == 0, "too many args");
            // nothing else to do
            break;
        case InstructionFormat::A1:
            asm_assert(args.size == 3, "too many args");
            instr |= parse_reg(args[0]) << 6;
            instr |= parse_reg(args[1]) << 3;
            instr |= parse_reg(args[2]);
            break;
        case InstructionFormat::A2:
            asm_assert(args.size == 2, "too many args");
            instr |= parse_reg(args[0]) << 6;
            instr |= parse_num(args[1], 4, nullptr);
            break;
        case InstructionFormat::A3:
            asm_assert(args.size == 1, "too many args");
            instr |= parse_reg(args[0]) << 6;
            break;
        case InstructionFormat::B1:
            asm_assert(args.size == 1, "too many args");
            instr |= parse_constant(args[0], addr, labels, 9);   // parse number or label
            break;
        case InstructionFormat::B2:
            asm_assert(args.size == 2, "too many args");
            instr |= parse_reg(args[0]) << 6;
            instr |= parse_num(args[1], 6, nullptr);
            break;
        case InstructionFormat::L1:
            asm_assert(args.size == 1, "too many args");
            if (op.upper)
            {
                instr |= 1 << 8;
            }
            // otherwise bit 8 remains 0
            instr |= parse_num(args[0], 8, nullptr);
            break;
        case InstructionFormat::L2:
            asm_assert(args.size == 2, "too many args");
            instr |= parse_reg(args[0]) << 6;
            instr |= parse_reg(args[1]) << 3;
            break;
    }

    return instr;
}

// remember to free()
char* read_entire_file(const char* filename)
{
    struct stat input_stat;
    stat(filename, &input_stat);      // TODO: portability
    char* file_buf = (char*)malloc(input_stat.st_size);

    FILE* input_file = fopen(filename, "rb");
    fread(file_buf, 1, input_stat.st_size, input_file);
    fclose(input_file);
    return file_buf;
}

struct Instr
{
    uint32_t addr;
    SubString op;
    Array<SubString, 3> args;
};

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        cout << "usage: ./assembler input_file output_file" << endl;
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];

    char* input_file_buf = read_entire_file(input_filename);
    to_upper(input_file_buf);

    Array<Instr, MAX_INSTR> instructions;
    Array<Label, MAX_LABELS> labels;

    bool in_comment = false;
    uint32_t next_addr = 0;

    char* file_pos = input_file_buf;
    while (file_pos[0])
    {
        SubString word = get_word(&file_pos);

        if (in_comment && word == "\n")
        {
            in_comment = false;
        }
        else if (is_comment(word))
        {
            in_comment = true;
        }
        else if (word == "ORG")
        {
            word = get_word(&file_pos);
            next_addr = parse_num(word, 16, nullptr);
        }
        else if (is_label(word))
        {
            // remove ':'
            --word.len;

            // check if it already exists
            for (uint32_t i = 0; i < labels.size; ++i)
            {
                asm_assert(labels[i].name != word, "duplicate label");
            }

            Label new_label = {.name = word, .addr = next_addr};
            labels.push(new_label);
        }
        else
        {
            uint32_t op_idx = lookup_op(word);
            if (op_idx != INVALID_OP)
            {
                Instr new_instr;
                new_instr.addr = next_addr;
                new_instr.op = word;

                word = get_word(&file_pos);
                while (!is_comment(word) && word != "\n")
                {
                    new_instr.args.push(word);
                    word = get_word(&file_pos);
                }

                instructions.push(new_instr);

                next_addr += 2;
            }
        }
    }

    uint8_t rom_data[ROM_SIZE] = {};

    for (uint32_t i = 0; i < instructions.size; ++i)
    {
        SubString op = instructions[i].op;
        Array<SubString, 3> args = instructions[i].args;
        uint32_t addr = instructions[i].addr;
        uint16_t instr = parse_instruction(op, args, labels, addr);
        rom_data[addr] = (instr >> 8) & 0xFF;
        rom_data[addr + 1] = instr & 0xFF;
        addr += 2;
    }



    // need fprintf because ofstream won't do padded hex
    FILE* output_file = fopen(output_filename, "w+");
    for (uint32_t i = 0; i < ROM_SIZE; i += 2)
    {
        fprintf(output_file, "%02X%02X\n", (uint32_t)rom_data[i], (uint32_t)rom_data[i + 1]);
    }
    fclose(output_file);
}
