def flat(arr, r = [])
  return r if arr.empty?

  if arr[0].empty?
    flat(arr[0][1..], r) # バグ. 正しくは flat(arr[1..], r)
  else
    flat(
      [arr[0][1..], *arr[1..]],
      [*r, arr[0][0]]
    )
  end
end

ROW_SIZE = 300
COL_SIZE = 200
table = Array.new(ROW_SIZE) { Array.new(COL_SIZE) { rand(100) } }
p flat(table)
