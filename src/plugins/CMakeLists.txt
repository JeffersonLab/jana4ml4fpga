# MIGRATION to JANA2 2026.x (see ai_rework/reports/): the legacy chain is RETIRED,
# not ported. gemrecon2 (thin adapter over the gemreco library) replaces the seven
# legacy gemrecon factories; CDAQfileMT replaces the -t block topology; Cozy/
# SpdlogMixin-based plugins are dropped. Data-model headers under gemrecon/ and
# fpgacon/ remain in the tree (included by flat_tree/gemrecon2) even though the
# plugins no longer build.

# --- active plugins ---
add_subdirectory(evio6_file)    # SRO streaming source: block->frames unfolder + RNTuple writer
add_subdirectory(CDAQfile)      # serial EVIO file source (fallback)
add_subdirectory(CDAQfileMT)    # mmap/parallel EVIO file source
add_subdirectory(flat_tree)     # events TTree writer
add_subdirectory(gemrecon2)     # GEM reconstruction adapter (gemreco library)

# --- to be ported after core migration ---
# add_subdirectory(cdaq)        # TCP/online source (SpdlogMixin + old source API)
# add_subdirectory(ml4fpa_dqm)  # TRD DQM processor
# add_subdirectory(fpgacon)     # FPGA exchange (Cozy factories)

# --- retired (JANA 2.1.x era; superseded or defunct) ---
# add_subdirectory(gemrecon)            # legacy 7-factory chain -> gemrecon2
# add_subdirectory(gemreco_fixdump)     # golden-fixture dumper (fixtures captured)
# add_subdirectory(test_cdaq)
# add_subdirectory(example_evio_analysis)
# add_subdirectory(single_event_evio)
# add_subdirectory(print_evio)
