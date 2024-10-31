(use-modules (guix gexp)
			 (guix packages)
			 (guix git-download)
             (guix build-system gnu)
			 ((guix licenses) #:prefix license:)
			 (gnu packages)
			 (gnu packages version-control))

(define (git-directory path)
  (computed-file
   (basename path)
   (with-imported-modules '((guix build utils))
	#~(begin
	   (use-modules (guix build utils))
	   (copy-recursively #$(local-file path #:recursive? #t) #$output)
	   (chdir #$output)
	   (invoke #$(file-append git "/bin/git") "clean" "-fdX")))))

(define fusemake
  (package
   (name "fusemake")
   (version "0.0.0")
   (source (git-directory (dirname (current-filename))))
   (build-system gnu-build-system)
   (arguments (list
			   #:phases
			   (with-imported-modules '((guix build utils))
				 #~(begin
					 (use-modules (guix build utils))
					 (modify-phases
					  %standard-phases
					  (delete 'configure)
					  (replace 'install
							   (lambda* (#:key outputs #:allow-other-keys)
								 (define bin (string-append (assoc-ref outputs "out") "/bin"))
								 (mkdir-p bin)
								 (install-file "fuzemake" bin))))))))
   (native-inputs (map specification->package '("pkg-config")))
   (inputs (map specification->package '("fuse")))
   (home-page "")
   (synopsis "Fusemake build system")
   (description "Use a FUSE file system to automatically make files")
   (license license:gpl3)))
fusemake
