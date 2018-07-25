#pragma once

#include <hwi/include/bqc/A2_inlines.h>

static inline uint64_t arch_timestamp_begin(void) { return GetTimeBase(); }
static inline uint64_t arch_timestamp_end(void) { return GetTimeBase(); }

#ifdef HAVE_BGPM

#include <bgpm/include/bgpm.h>

struct pmu_evt {
  const char *name;
  unsigned id;
};

struct pmu_evt events[] = {
    {"PEVT_LSU_COMMIT_LD_MISSES", PEVT_LSU_COMMIT_LD_MISSES},
    {"PEVT_L2_MISSES", PEVT_L2_MISSES}};

int find_event(const char *const name, unsigned *id) {
  const unsigned elems = sizeof(events) / sizeof(struct pmu_evt);
  for (unsigned i = 0; i < elems; ++i) {
    if (strcasecmp(name, events[i].name) == 0) {
      if (id) {
        *id = events[i].id;
      }
      return 0;
    }
  }
  return -1;
}

struct pmu {
  int evt_set;
};

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  if (num_pmcs == 0) {
    return NULL;
  }

  if (Bgpm_Init(BGPM_MODE_SWDISTRIB)) {
    perror("Bgpm_Iinit()");
    exit(EXIT_FAILURE);
  }

  struct pmu *pmu = (struct pmu *)malloc(sizeof(struct pmu));
  if (pmu == NULL) {
    fprintf(stderr, "Allocating struct pmu failed\n");
    exit(EXIT_FAILURE);
  }

  pmu->evt_set = Bgpm_CreateEventSet();
  if (pmu->evt_set < 0) {
    fprintf(stderr, "Bgpm_CreateEventSet() failed: %d\n", pmu->evt_set);
    exit(EXIT_FAILURE);
  }

  for (unsigned i = 0; i < num_pmcs; ++i) {
    unsigned id;
    int err = find_event(pmcs[i], &id);
    if (err < 0) {
      fprintf(stderr, "Event %s not found.\n", pmcs[i]);
      continue;
    }

    err = Bgpm_AddEvent(pmu->evt_set, id);
    if (err < 0) {
      fprintf(stderr, "Bgpm_AddEvent() failed: %d\n", err);
      exit(EXIT_FAILURE);
    }
  }

  const int err = Bgpm_Apply(pmu->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Apply() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  return pmu;
}

static void arch_pmu_free(struct pmu *pmus) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_DeleteEventSet(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_DeleteEventSet() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_Disable();
  if (err < 0) {
    fprintf(stderr, "Bgpm_Disable() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  free(pmus);
}

static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_Reset(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Reset() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_Start(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Start() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }
}

static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_Stop(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Stop() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_ReadEvent(pmus->evt_set, 0, data);
  if (err < 0) {
    fprintf(stderr, "Bgpm_ReadEvent() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }
}

#else /* HAVE_BGPM */

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#endif /* HAVE_BGPM */

