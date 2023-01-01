def select(arr, &b)
  select_r(arr, [], &b)
end

def select_r(arr, r, &b)
  return r if arr.empty?

  if b.(arr[0])
    select_r(arr[1..], [*r, arr[0]], &b)
  else
    select_r(arr[1..], r, &b)
  end
end
