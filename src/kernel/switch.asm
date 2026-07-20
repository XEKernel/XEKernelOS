; switch_to(prev, next)
; prev = pointer to current task_struct pointer (task_struct**)
; next = pointer to next task_struct
global switch_to
switch_to:
    mov eax, [esp + 4]          ; eax = prev (&current_task)
    mov eax, [eax]              ; eax = *prev = current_task pointer
    mov [eax + 4], ecx          ; save ecx
    mov [eax + 8], edx          ; save edx
    mov [eax + 12], ebp
    mov [eax + 16], ebx
    mov [eax + 20], esi
    mov [eax + 24], edi
    mov ecx, [esp]              ; ecx = return address
    mov [eax], ecx              ; save eip (task_struct offset 0)
    lea ecx, [esp + 8]          ; ecx = ESP after ret pops return addr + args
    mov [eax + 28], ecx         ; save esp (offset 28 in task_struct)

    mov eax, [esp + 8]          ; eax = next task pointer
    mov ecx, [eax]              ; ecx = next->eip
    mov edx, [eax + 4]          ; edx = next->ecx
    mov [esp + 4], edx          ; restore ecx later via stack
    mov edx, [eax + 8]          ; edx = next->edx
    mov [esp + 8], edx          ; restore edx later via stack
    mov ebp, [eax + 12]
    mov ebx, [eax + 16]
    mov esi, [eax + 20]
    mov edi, [eax + 24]
    mov esp, [eax + 28]         ; switch stack

    mov eax, [esp - 8]          ; recover ecx
    mov edx, [esp - 4]          ; recover edx
    push ecx                    ; push next->eip as return address
    ret
