open Durable7

let test_hash_policy () =
  let policy = Common.Hash_policy.create ~hash:String.length ~equal:String.equal in
  Alcotest.(check int) "hash" 5 (Common.Hash_policy.hash policy "ocaml");
  Alcotest.(check bool) "equal" true (Common.Hash_policy.equal policy "port" "port");
  Alcotest.(check bool) "distinct" false (Common.Hash_policy.equal policy "port" "ports");
  Alcotest.(check bool) "identity" true (Common.Hash_policy.same policy policy);
  Alcotest.(check bool)
    "separate policies" false
    (Common.Hash_policy.same policy
       (Common.Hash_policy.create ~hash:String.length ~equal:String.equal))

let test_comparator () =
  let ascending = Common.Comparator.default () in
  let descending = Common.Comparator.reverse ascending in
  Alcotest.(check int) "ascending" (-1) (Common.Comparator.compare ascending 1 2);
  Alcotest.(check int) "descending" 1 (Common.Comparator.compare descending 1 2);
  Alcotest.(check bool) "identity" true (Common.Comparator.same ascending ascending);
  Alcotest.(check bool) "reverse is distinct" false (Common.Comparator.same ascending descending)

let () =
  Alcotest.run "common policies"
    [
      ( "policy",
        [
          Alcotest.test_case "hash/equality" `Quick test_hash_policy;
          Alcotest.test_case "comparison" `Quick test_comparator;
        ] );
    ]
