for i in {1..64}
do echo "$i$(./$1 -t $i -o 10000 | grep -o ': .* ms')"
done
