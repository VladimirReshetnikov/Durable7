type byte_order = Little_endian | Big_endian

module type S = sig
  type t

  val width : int
  val signed : bool
  val zero : t
  val one : t
  val min_value : t
  val max_value : t
  val of_int : int -> t
  val of_z : Z.t -> t
  val of_z_checked : Z.t -> t
  val to_z : t -> Z.t
  val to_unsigned_z : t -> Z.t
  val parse : ?radix:int -> string -> t
  val to_string : ?radix:int -> t -> string
  val equal : t -> t -> bool
  val compare : t -> t -> int
  val add : t -> t -> t
  val sub : t -> t -> t
  val mul : t -> t -> t
  val div : t -> t -> t
  val rem : t -> t -> t
  val div_rem : t -> t -> t * t
  val negate : t -> t
  val abs : t -> t
  val succ : t -> t
  val pred : t -> t
  val checked_add : t -> t -> t
  val checked_sub : t -> t -> t
  val checked_mul : t -> t -> t
  val checked_negate : t -> t
  val logand : t -> t -> t
  val logor : t -> t -> t
  val logxor : t -> t -> t
  val lognot : t -> t
  val shift_left : t -> int -> t
  val shift_right : t -> int -> t
  val rotate_left : t -> int -> t
  val rotate_right : t -> int -> t
  val is_zero : t -> bool
  val is_negative : t -> bool
  val is_even : t -> bool
  val is_power_of_two : t -> bool
  val shortest_bit_length : t -> int
  val leading_zero_count : t -> int
  val trailing_zero_count : t -> int
  val pop_count : t -> int
  val log2 : t -> int
  val byte_count : int
  val to_bytes : ?byte_order:byte_order -> t -> bytes
  val of_bytes : ?byte_order:byte_order -> bytes -> t
end

module type Parameters = sig
  val width : int
  val signed : bool
end

module Make (Parameters : Parameters) : S = struct
  type t = Z.t

  let width = Parameters.width
  let signed = Parameters.signed
  let modulus = Z.shift_left Z.one width
  let sign_bit = Z.shift_left Z.one (width - 1)
  let minimum = if signed then Z.neg sign_bit else Z.zero
  let maximum = if signed then Z.pred sign_bit else Z.pred modulus
  let byte_count = width / 8

  let unsigned value =
    let reduced = Z.erem value modulus in
    if Z.sign reduced < 0 then Z.add reduced modulus else reduced

  let normalize value =
    let bits = unsigned value in
    if signed && Z.compare bits sign_bit >= 0 then Z.sub bits modulus else bits

  let of_z = normalize

  let of_z_checked value =
    if Z.compare value minimum < 0 || Z.compare value maximum > 0 then
      invalid_arg (Printf.sprintf "value does not fit in %d bits" width);
    value

  let to_z value = value
  let to_unsigned_z = unsigned
  let of_int value = of_z (Z.of_int value)
  let zero = of_int 0
  let one = of_int 1
  let min_value = minimum
  let max_value = maximum
  let equal = Z.equal
  let compare = Z.compare
  let add left right = of_z (Z.add left right)
  let sub left right = of_z (Z.sub left right)
  let mul left right = of_z (Z.mul left right)

  let check_division dividend divisor =
    if Z.equal divisor Z.zero then raise Division_by_zero;
    if signed && Z.equal divisor Z.minus_one && Z.equal dividend minimum then
      invalid_arg "signed division overflow"

  let div left right =
    check_division left right;
    of_z (Z.div left right)

  let rem left right =
    check_division left right;
    of_z (Z.rem left right)

  let div_rem left right = (div left right, rem left right)
  let negate value = of_z (Z.neg value)

  let abs value =
    if signed && Z.equal value minimum then invalid_arg "absolute value overflow";
    of_z (Z.abs value)

  let succ value = add value one
  let pred value = sub value one
  let checked_add left right = of_z_checked (Z.add left right)
  let checked_sub left right = of_z_checked (Z.sub left right)
  let checked_mul left right = of_z_checked (Z.mul left right)
  let checked_negate value = of_z_checked (Z.neg value)
  let logand left right = of_z (Z.logand (unsigned left) (unsigned right))
  let logor left right = of_z (Z.logor (unsigned left) (unsigned right))
  let logxor left right = of_z (Z.logxor (unsigned left) (unsigned right))
  let lognot value = of_z (Z.lognot (unsigned value))

  let normalized_shift count =
    let reduced = count mod width in
    if reduced < 0 then reduced + width else reduced

  let shift_left value count = of_z (Z.shift_left (unsigned value) (normalized_shift count))

  let shift_right value count =
    let source = if signed then value else unsigned value in
    of_z (Z.shift_right source (normalized_shift count))

  let rotate_left value count =
    let shift = normalized_shift count in
    if shift = 0 then value
    else
      of_z
        (Z.logor
           (Z.shift_left (unsigned value) shift)
           (Z.shift_right (unsigned value) (width - shift)))

  let rotate_right value count = rotate_left value (-count)
  let is_zero value = Z.equal value Z.zero
  let is_negative value = signed && Z.sign value < 0
  let is_even value = not (Z.testbit (unsigned value) 0)
  let is_power_of_two value = Z.sign value > 0 && Z.equal (Z.logand value (Z.pred value)) Z.zero

  let shortest_bit_length value =
    if Z.equal value Z.zero then 0
    else if not signed then Z.numbits value
    else if Z.sign value > 0 then Z.numbits value + 1
    else Z.numbits (Z.lognot value) + 1

  let leading_zero_count value = width - Z.numbits (unsigned value)

  let trailing_zero_count value =
    let bits = unsigned value in
    if Z.equal bits Z.zero then width else Z.trailing_zeros bits

  let pop_count value = Z.popcount (unsigned value)

  let log2 value =
    if Z.sign value < 0 then invalid_arg "log2 is undefined for negative values";
    if Z.equal value Z.zero then 0 else Z.numbits value - 1

  let to_bytes ?(byte_order = Little_endian) value =
    let result = Bytes.make byte_count '\000' in
    let bits = unsigned value in
    for index = 0 to byte_count - 1 do
      let byte = Z.to_int (Z.extract bits (index * 8) 8) in
      let destination =
        match byte_order with Little_endian -> index | Big_endian -> byte_count - index - 1
      in
      Bytes.set result destination (Char.chr byte)
    done;
    result

  let of_bytes ?(byte_order = Little_endian) bytes =
    if Bytes.length bytes <> byte_count then
      invalid_arg (Printf.sprintf "expected exactly %d bytes" byte_count);
    let result = ref Z.zero in
    for index = 0 to byte_count - 1 do
      let source =
        match byte_order with Little_endian -> index | Big_endian -> byte_count - index - 1
      in
      let byte = Z.of_int (Char.code (Bytes.get bytes source)) in
      result := Z.logor !result (Z.shift_left byte (index * 8))
    done;
    of_z !result

  let parse ?(radix = 10) text =
    if radix < 2 || radix > 36 then invalid_arg "radix must be between 2 and 36";
    let stripped = String.trim text in
    if String.length stripped = 0 then invalid_arg "input is not an integer";
    let value = Z.of_string_base radix stripped in
    let explicitly_negative = stripped.[0] = '-' in
    if signed && radix <> 10 && not explicitly_negative then (
      if Z.sign value < 0 || Z.compare value modulus >= 0 then
        invalid_arg (Printf.sprintf "value does not fit in %d bits" width);
      of_z value)
    else of_z_checked value

  let digits radix value =
    if Z.equal value Z.zero then "0"
    else
      let alphabet = "0123456789abcdefghijklmnopqrstuvwxyz" in
      let negative = Z.sign value < 0 in
      let remaining = ref (Z.abs value) in
      let buffer = Buffer.create 32 in
      let radix_z = Z.of_int radix in
      while not (Z.equal !remaining Z.zero) do
        let quotient, remainder = Z.ediv_rem !remaining radix_z in
        Buffer.add_char buffer alphabet.[Z.to_int remainder];
        remaining := quotient
      done;
      if negative then Buffer.add_char buffer '-';
      let reversed = Buffer.contents buffer in
      String.init (String.length reversed) (fun index ->
          reversed.[String.length reversed - index - 1])

  let to_string ?(radix = 10) value =
    if radix < 2 || radix > 36 then invalid_arg "radix must be between 2 and 36";
    digits radix (if signed && radix <> 10 then unsigned value else value)
end

module UInt256 = Make (struct
  let width = 256
  let signed = false
end)

module Int256 = Make (struct
  let width = 256
  let signed = true
end)

module UInt512 = Make (struct
  let width = 512
  let signed = false
end)

module Int512 = Make (struct
  let width = 512
  let signed = true
end)

module UInt1024 = Make (struct
  let width = 1024
  let signed = false
end)

module Int1024 = Make (struct
  let width = 1024
  let signed = true
end)

module Bit_converter_ex = struct
  let try_write_bytes (type value) (module Integer : S with type t = value)
      ?(byte_order = Little_endian) value destination ~offset =
    if offset < 0 || offset > Bytes.length destination - Integer.byte_count then false
    else (
      Bytes.blit (Integer.to_bytes ~byte_order value) 0 destination offset Integer.byte_count;
      true)
end
