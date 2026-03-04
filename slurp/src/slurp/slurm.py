"""Slurm command wrappers."""

from __future__ import annotations

import asyncio
import subprocess
from dataclasses import dataclass
from typing import Optional


@dataclass
class Job:
    """Represents a Slurm job."""

    job_id: str
    name: str
    user: str
    partition: str
    state: str
    time: str
    nodes: str
    nodelist: str

    @property
    def state_symbol(self) -> str:
        """Return a symbol representing the job state."""
        symbols = {
            "RUNNING": "R",
            "PENDING": "P",
            "COMPLETED": "C",
            "FAILED": "F",
            "CANCELLED": "X",
            "TIMEOUT": "T",
            "PREEMPTED": "PR",
            "NODE_FAIL": "NF",
            "SUSPENDED": "S",
        }
        return symbols.get(self.state, "?")


async def get_jobs(user: Optional[str] = None) -> list[Job]:
    """Fetch jobs from squeue.

    Args:
        user: Filter by username. If None, shows all jobs.

    Returns:
        List of Job objects.
    """
    cmd = [
        "squeue",
        "--format=%i|%j|%u|%P|%T|%M|%D|%R",
        "--noheader",
    ]

    if user:
        cmd.extend(["--user", user])

    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, stderr = await proc.communicate()

    if proc.returncode != 0:
        # squeue not available or error
        return []

    jobs = []
    for line in stdout.decode().strip().split("\n"):
        if not line:
            continue
        parts = line.split("|")
        if len(parts) >= 8:
            jobs.append(
                Job(
                    job_id=parts[0].strip(),
                    name=parts[1].strip(),
                    user=parts[2].strip(),
                    partition=parts[3].strip(),
                    state=parts[4].strip(),
                    time=parts[5].strip(),
                    nodes=parts[6].strip(),
                    nodelist=parts[7].strip(),
                )
            )

    return jobs


async def cancel_job(job_id: str) -> tuple[bool, str]:
    """Cancel a job using scancel.

    Args:
        job_id: The job ID to cancel.

    Returns:
        Tuple of (success, message).
    """
    proc = await asyncio.create_subprocess_exec(
        "scancel",
        job_id,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    _, stderr = await proc.communicate()

    if proc.returncode == 0:
        return True, f"Job {job_id} cancelled"
    else:
        return False, stderr.decode().strip() or f"Failed to cancel job {job_id}"


async def get_job_info(job_id: str) -> Optional[str]:
    """Get detailed job information using scontrol.

    Args:
        job_id: The job ID to query.

    Returns:
        Job info string or None if failed.
    """
    proc = await asyncio.create_subprocess_exec(
        "scontrol",
        "show",
        "job",
        job_id,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, _ = await proc.communicate()

    if proc.returncode == 0:
        return stdout.decode()
    return None


async def hold_job(job_id: str) -> tuple[bool, str]:
    """Hold a pending job.

    Args:
        job_id: The job ID to hold.

    Returns:
        Tuple of (success, message).
    """
    proc = await asyncio.create_subprocess_exec(
        "scontrol",
        "hold",
        job_id,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    _, stderr = await proc.communicate()

    if proc.returncode == 0:
        return True, f"Job {job_id} held"
    else:
        return False, stderr.decode().strip() or f"Failed to hold job {job_id}"


async def release_job(job_id: str) -> tuple[bool, str]:
    """Release a held job.

    Args:
        job_id: The job ID to release.

    Returns:
        Tuple of (success, message).
    """
    proc = await asyncio.create_subprocess_exec(
        "scontrol",
        "release",
        job_id,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    _, stderr = await proc.communicate()

    if proc.returncode == 0:
        return True, f"Job {job_id} released"
    else:
        return False, stderr.decode().strip() or f"Failed to release job {job_id}"
