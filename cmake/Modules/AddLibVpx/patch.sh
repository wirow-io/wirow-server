#/bin/sh

cat << EOF > ./.patch
diff --git a/configure b/configure
index da631a45e..c8289c2ab 100755
--- a/configure
+++ b/configure
@@ -171,9 +171,9 @@ for t in ${all_targets}; do
     [ -f "${source_path}/${t}.mk" ] && enable_feature ${t}
 done

-if ! diff --version >/dev/null; then
-  die "diff missing: Try installing diffutils via your package manager."
-fi
+#if ! diff --version >/dev/null; then
+#  die "diff missing: Try installing diffutils via your package manager."
+#fi

 if ! perl --version >/dev/null; then
     die "Perl is required to build"
EOF

patch -p1 < ./.patch
