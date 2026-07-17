type 'element t = { deque : 'element Persistent_deque.t; reversed : bool }

let empty = { deque = Persistent_deque.empty; reversed = false }
let of_list values = { deque = Persistent_deque.of_list values; reversed = false }
let length value = Persistent_deque.length value.deque
let is_empty value = Persistent_deque.is_empty value.deque
let reverse value = { value with reversed = not value.reversed }

let cons element value =
  if value.reversed then { value with deque = Persistent_deque.snoc value.deque element }
  else { value with deque = Persistent_deque.cons element value.deque }

let snoc value element =
  if value.reversed then { value with deque = Persistent_deque.cons element value.deque }
  else { value with deque = Persistent_deque.snoc value.deque element }

let first value =
  if value.reversed then Persistent_deque.last value.deque else Persistent_deque.first value.deque

let last value =
  if value.reversed then Persistent_deque.first value.deque else Persistent_deque.last value.deque

let pop_front value =
  if value.reversed then
    Option.map
      (fun (successor, element) -> (element, { value with deque = successor }))
      (Persistent_deque.pop_back value.deque)
  else
    Option.map
      (fun (element, successor) -> (element, { value with deque = successor }))
      (Persistent_deque.pop_front value.deque)

let pop_back value =
  if value.reversed then
    Option.map
      (fun (element, successor) -> ({ value with deque = successor }, element))
      (Persistent_deque.pop_front value.deque)
  else
    Option.map
      (fun (successor, element) -> ({ value with deque = successor }, element))
      (Persistent_deque.pop_back value.deque)

let nth index value =
  if value.reversed then Persistent_deque.nth (length value - index - 1) value.deque
  else Persistent_deque.nth index value.deque

let to_list value =
  let values = Persistent_deque.to_list value.deque in
  if value.reversed then List.rev values else values

let concat left right = of_list (to_list left @ to_list right)
