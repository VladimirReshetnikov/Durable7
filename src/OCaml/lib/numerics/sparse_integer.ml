type t = Z.t

let of_z value =
  if Z.sign value < 0 then invalid_arg "Sparse_integer cannot be negative";
  value

let of_int value = of_z (Z.of_int value)
let zero = of_int 0
let one = of_int 1
let to_z value = value
let to_string = Z.to_string
let equal = Z.equal
let compare = Z.compare
let is_zero value = Z.equal value Z.zero
let add left right = Z.add left right
let mul left right = Z.mul left right

let parse text =
  let stripped = String.trim text in
  let unsigned =
    if String.length stripped > 0 && stripped.[0] = '+' then
      String.sub stripped 1 (String.length stripped - 1)
    else stripped
  in
  if String.length unsigned = 0 then invalid_arg "invalid Sparse_integer";
  of_z (Z.of_string unsigned)

let power_of_two exponent =
  if not (Z.fits_int exponent) then invalid_arg "exponent is too large";
  Z.shift_left Z.one (Z.to_int exponent)

let power_of_two_int exponent =
  if exponent < 0 then invalid_arg "exponent cannot be negative";
  Z.shift_left Z.one exponent

let exact_log2 value =
  if Z.equal value Z.zero || not (Z.equal (Z.logand value (Z.pred value)) Z.zero) then
    invalid_arg "value is not an exact power of two";
  Z.of_int (Z.numbits value - 1)
