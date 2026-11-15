"""Owned mask-UV additions; native sampling and RGB arithmetic stay in place."""
import struct
import native_fragment_ssa_probe as ssa


def transform(binary, current):
    words = struct.unpack('<%dI' % (len(binary) // 4), binary)
    header, code, at = list(words[:5]), [], 5
    while at < len(words):
        count = words[at] >> 16
        ssa.require(count and at + count <= len(words), 'complete coverage instructions')
        code.append(ssa.Instruction(words[at] & 65535, tuple(words[at+1:at+count])))
        at += count
    make = lambda op, *args: ssa.Instruction(op, tuple(args))
    fresh = iter(range(header[3], header[3] + 192))
    allocate = lambda: next(fresh)
    scalar, vector, boolean = (6, 27, 128) if current else (8, 9, next(i.operands[0] for i in code if i.opcode == 20))
    zero, one, index, weight = (94, 68, 221, 665) if current else (210, 185, 95, 1008)
    vector2 = next(i.operands[0] for i in code if i.opcode == 23 and i.operands[1:] == (scalar, 2))
    bool4 = next(i.operands[0] for i in code if i.opcode == 23 and i.operands[1:] == (boolean, 4))
    # Find OUR set1/binding0 variable, independently of its generated SSA ID.
    sets = {i.operands[0]: i.operands[2] for i in code if i.opcode == 71 and i.operands[1] == 34}
    bindings = {i.operands[0]: i.operands[2] for i in code if i.opcode == 71 and i.operands[1] == 33}
    mask = [ident for ident, value in sets.items() if value == 1 and bindings.get(ident) == 0]
    ssa.require(len(mask) == 1, 'one owned mask descriptor')
    control = [ident for ident, value in sets.items() if value == 2 and bindings.get(ident) == 0]
    ssa.require(len(control) == 1, 'one owned exact-picture control')
    scalar_pointer = next(i.operands[0] for i in code if i.opcode == 32 and i.operands[1:] == (2, scalar))
    samplers = {i.operands[1] for i in code if i.opcode == 61 and i.operands[2] == mask[0]}
    block, block_pointer, uniform, vector_pointer, zero_rgba = [allocate() for _ in range(5)]
    decoration = [make(71, block, 2), make(72, block, 0, 35, 0), make(71, uniform, 34, 1), make(71, uniform, 33, 1)]
    declaration = [make(30, block, vector), make(32, block_pointer, 2, block),
                   make(32, vector_pointer, 2, vector), make(59, block_pointer, uniform, 2),
                   make(44, vector, zero_rgba, zero, zero, zero, zero)]
    existing = next((i.operands[0] for i in code if i.opcode == 32 and i.operands[1:] == (2, vector)), None)
    if existing is not None:
        declaration = [i for i in declaration if not (i.opcode == 32 and i.operands[0] == vector_pointer)]
        vector_pointer = existing

    def load_mapping():
        address, value = allocate(), allocate()
        return [make(65, vector_pointer, address, uniform, index), make(61, vector, value, address)], value

    result, mask_validity, scalar_validity, merged = [], {}, {}, []
    annotations = declarations = False
    for item in code:
        if not annotations and item.opcode in ssa.TYPE_OPS:
            result.extend(decoration)
            annotations = True
        if not declarations and item.opcode == 54:
            result.extend(declaration)
            declarations = True
        if item.opcode == 88 and item.operands[2] in samplers:
            prefix, mapping = load_mapping()
            scale, bias, product, uv = [allocate() for _ in range(4)]
            prefix += [make(79, vector2, scale, mapping, mapping, 0, 1),
                       make(79, vector2, bias, mapping, mapping, 2, 3),
                       make(133, vector2, product, item.operands[3], scale), make(129, vector2, uv, product, bias)]
            valid = []
            for component in (0, 1):
                value, lower, upper, inside = [allocate() for _ in range(4)]
                prefix += [make(81, scalar, value, uv, component), make(190, boolean, lower, value, zero),
                           make(188, boolean, upper, value, one), make(167, boolean, inside, lower, upper)]
                valid.append(inside)
            both, numeric = allocate(), allocate()
            prefix += [make(167, boolean, both, *valid), make(169, scalar, numeric, both, one, zero)]
            mask_validity[item.operands[1]] = numeric
            result.extend(prefix)
            arguments = list(item.operands)
            arguments[3] = uv
            item = make(item.opcode, *arguments)
        if item.opcode == 81 and item.operands[2] in mask_validity:
            scalar_validity[item.operands[1]] = mask_validity[item.operands[2]]
        if item.opcode == 245 and any(value in scalar_validity for value in item.operands[2::2]):
            arguments = list(item.operands)
            arguments[1] = allocate()
            for n in range(2, len(arguments), 2):
                arguments[n] = scalar_validity.get(arguments[n], one)
            result.append(make(245, *arguments))
            merged.append(arguments[1])
        if item.opcode == 62 and item.operands[0] == (337 if current else 504):
            values = merged if current else list(scalar_validity.values())
            ssa.require(len(values) == 2, 'two native outer/inner mask paths')
            prefix, mapping = load_mapping()
            identity = []
            for component in range(4):
                value, equal = allocate(), allocate()
                prefix += [make(81, scalar, value, mapping, component), make(180, boolean, equal, value, one if component < 2 else zero)]
                identity.append(equal)
            a, b, unchanged = allocate(), allocate(), allocate()
            prefix += [make(167, boolean, a, identity[0], identity[1]), make(167, boolean, b, identity[2], identity[3]), make(167, boolean, unchanged, a, b)]
            # A native branch with zero blend weight contributes no RGB. An
            # out-of-visible sample with a nonzero weight refuses the pixel as
            # zero RGBA; an alpha-zero colour must never hide padding RGB.
            allowed = []
            for valid, unused_weight in zip(values, (one, zero)):
                inside, unused, safe = allocate(), allocate(), allocate()
                prefix += [make(180, boolean, inside, valid, one), make(180, boolean, unused, weight, unused_weight), make(166, boolean, safe, inside, unused)]
                allowed.append(safe)
            both, cropped_safe, address, exact, ordinary, safe, condition, output = [allocate() for _ in range(8)]
            prefix += [make(167, boolean, both, *allowed), make(166, boolean, cropped_safe, unchanged, both),
                       make(65, scalar_pointer, address, control[0], index), make(61, scalar, exact, address),
                       make(180, boolean, ordinary, exact, zero), make(166, boolean, safe, ordinary, cropped_safe),
                       make(80, bool4, condition, safe, safe, safe, safe), make(169, vector, output, condition, item.operands[1], zero_rgba)]
            result.extend(prefix)
            item = make(62, item.operands[0], output)
        result.append(item)
    ssa.require(annotations and declarations and len(mask_validity) == 2, 'complete owned geometry framing')
    header[3] = next(fresh)
    return struct.pack('<%dI' % (5 + sum(len(i.operands) + 1 for i in result)), *header,
                       *(word for item in result for word in item.words()))
