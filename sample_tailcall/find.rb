def find(arr, &b)
  find_r(arr, [], &b)
end

def find_r(arr, r, &b)
  return nil if arr.empty?

  if b.(arr[0])
    arr[0]
  else
    find_r(arr[1..], r, &b)
  end
end

arr = Array.new(10000) { rand(100) }
p find(arr) { |n| sum(n) > 100 }
