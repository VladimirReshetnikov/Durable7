open Tools_data_structures
open Numerics.Wide_integer

let check_wrap (type value) (module Integer : S with type t = value) () =
  let wrapped = Integer.add Integer.max_value Integer.one in
  let expected = if Integer.signed then Integer.min_value else Integer.zero in
  Alcotest.(check bool) "wrap" true (Integer.equal expected wrapped);
  Alcotest.check_raises "checked overflow"
    (Invalid_argument (Printf.sprintf "value does not fit in %d bits" Integer.width))
    (fun () -> ignore (Integer.checked_add Integer.max_value Integer.one))

let test_all_widths_wrap () =
  check_wrap (module UInt256) ();
  check_wrap (module Int256) ();
  check_wrap (module UInt512) ();
  check_wrap (module Int512) ();
  check_wrap (module UInt1024) ();
  check_wrap (module Int1024) ()

let test_twos_complement_bytes () =
  let minus_one = Int256.of_int (-1) in
  let expected = Bytes.make Int256.byte_count '\255' in
  Alcotest.(check bytes) "little endian" expected (Int256.to_bytes minus_one);
  Alcotest.(check bytes) "big endian" expected (Int256.to_bytes ~byte_order:Big_endian minus_one);
  Alcotest.(check bool)
    "hex parse" true
    (Int256.equal minus_one (Int256.parse ~radix:16 (String.make 64 'f')));
  Alcotest.(check bool)
    "round trip" true
    (Int256.equal minus_one (Int256.of_bytes (Int256.to_bytes minus_one)))

let test_bit_diagnostics () =
  let value = UInt256.of_int 0b1011_0000 in
  Alcotest.(check int) "leading zeros" 248 (UInt256.leading_zero_count value);
  Alcotest.(check int) "trailing zeros" 4 (UInt256.trailing_zero_count value);
  Alcotest.(check int) "population" 3 (UInt256.pop_count value);
  Alcotest.(check int) "log2" 7 (UInt256.log2 value);
  Alcotest.(check bool) "even" true (UInt256.is_even value);
  let rotated = UInt256.rotate_right (UInt256.rotate_left value 73) 73 in
  Alcotest.(check bool) "rotate round trip" true (UInt256.equal value rotated)

let test_division () =
  let quotient, remainder = Int256.div_rem (Int256.of_int (-7)) (Int256.of_int 3) in
  Alcotest.(check string) "truncating quotient" "-2" (Int256.to_string quotient);
  Alcotest.(check string) "signed remainder" "-1" (Int256.to_string remainder);
  Alcotest.check_raises "division by zero" Division_by_zero (fun () ->
      ignore (UInt256.div UInt256.one UInt256.zero))

let arithmetic_property =
  QCheck.Test.make ~count:200 ~name:"UInt256 arithmetic is normalized modulo 2^256"
    QCheck.(pair (int_range (-1_000_000) 1_000_000) (int_range (-1_000_000) 1_000_000))
    (fun (left, right) ->
      let left_value = UInt256.of_int left in
      let right_value = UInt256.of_int right in
      Z.equal
        (UInt256.to_unsigned_z (UInt256.add left_value right_value))
        (Z.erem (Z.add (Z.of_int left) (Z.of_int right)) (Z.shift_left Z.one 256)))

let test_arithmetic_property () = QCheck.Test.check_exn arithmetic_property

let test_bit_converter () =
  let value = UInt512.of_z (Z.of_string "123456789012345678901234567890") in
  let destination = Bytes.make (UInt512.byte_count + 4) '\000' in
  Alcotest.(check bool)
    "write" true
    (Bit_converter_ex.try_write_bytes (module UInt512) value destination ~offset:2);
  let encoded = Bytes.sub destination 2 UInt512.byte_count in
  Alcotest.(check bool) "read" true (UInt512.equal value (UInt512.of_bytes encoded));
  Alcotest.(check bool)
    "undersized" false
    (Bit_converter_ex.try_write_bytes (module UInt512) value (Bytes.create 2) ~offset:0)

let test_sparse_integer () =
  let open Numerics.Sparse_integer in
  let power = power_of_two_int 1000 in
  Alcotest.(check string) "exact log2" "1000" (to_string (exact_log2 power));
  Alcotest.(check bool)
    "addition" true
    (Z.equal (to_z (add power one)) (Z.add (Z.shift_left Z.one 1000) Z.one));
  Alcotest.(check string) "multiplication" "56088" (to_string (mul (of_int 123) (of_int 456)));
  Alcotest.(check string) "parse" "123456" (to_string (parse "  +123456 "))

let () =
  Alcotest.run "numerics"
    [
      ( "wide integers",
        [
          Alcotest.test_case "wrapping and checked overflow" `Quick test_all_widths_wrap;
          Alcotest.test_case "two's-complement bytes" `Quick test_twos_complement_bytes;
          Alcotest.test_case "bit diagnostics" `Quick test_bit_diagnostics;
          Alcotest.test_case "division" `Quick test_division;
          Alcotest.test_case "model property" `Quick test_arithmetic_property;
          Alcotest.test_case "bit converter" `Quick test_bit_converter;
        ] );
      ("sparse integer", [ Alcotest.test_case "powers and arithmetic" `Quick test_sparse_integer ]);
    ]
