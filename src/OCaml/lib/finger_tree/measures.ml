type 'measure monoid = { monoid_empty : 'measure; monoid_append : 'measure -> 'measure -> 'measure }

type ('element, 'measure) policy = {
  policy_name : string;
  policy_monoid : 'measure monoid;
  policy_measure : 'element -> 'measure;
}

let create_monoid ~empty ~append = { monoid_empty = empty; monoid_append = append }

let create_policy ~id ~monoid ~measure () =
  if String.trim id = "" then invalid_arg "measure policy id must not be empty";
  { policy_name = id; policy_monoid = monoid; policy_measure = measure }

let policy_id policy = policy.policy_name
let empty policy = policy.policy_monoid.monoid_empty
let append policy = policy.policy_monoid.monoid_append
let measure policy = policy.policy_measure
let compatible left right = String.equal left.policy_name right.policy_name

let size =
  {
    policy_name = "size-v1";
    policy_monoid = { monoid_empty = 0; monoid_append = ( + ) };
    policy_measure = (fun _ -> 1);
  }

let int_sum =
  create_policy ~id:"int-sum-v1" ~monoid:(create_monoid ~empty:0 ~append:( + )) ~measure:Fun.id ()

let int64_sum =
  create_policy ~id:"int64-sum-v1"
    ~monoid:(create_monoid ~empty:0L ~append:Int64.add)
    ~measure:Fun.id ()

let pair ~id left right =
  create_policy ~id
    ~monoid:
      (create_monoid
         ~empty:(empty left, empty right)
         ~append:(fun (left_a, right_a) (left_b, right_b) ->
           (append left left_a left_b, append right right_a right_b)))
    ~measure:(fun value -> (measure left value, measure right value))
    ()

let optional_extreme ~id ~compare ~choose =
  create_policy ~id
    ~monoid:
      (create_monoid ~empty:None ~append:(fun left right ->
           match (left, right) with
           | None, value | value, None -> value
           | Some left_value, Some right_value -> Some (choose compare left_value right_value)))
    ~measure:(fun value -> Some value)
    ()

let optional_max ~id ~compare =
  optional_extreme ~id ~compare ~choose:(fun compare left right ->
      if compare left right >= 0 then left else right)

let optional_min ~id ~compare =
  optional_extreme ~id ~compare ~choose:(fun compare left right ->
      if compare left right <= 0 then left else right)
