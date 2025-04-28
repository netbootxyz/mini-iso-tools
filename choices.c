choices_t* choices_create(size_t capacity) {
    choices_t* choices = malloc(sizeof(choices_t));
    if (!choices) {
        syslog(LOG_DEBUG, "Failed to allocate choices structure");
        return NULL;
    }

    choices->values = calloc(capacity, sizeof(iso_data_t*));
    if (!choices->values) {
        syslog(LOG_DEBUG, "Failed to allocate choices values array");
        free(choices);
        return NULL;
    }

    choices->len = 0;
    choices->cap = capacity;
    choices->cur = 0;

    syslog(LOG_DEBUG, "Created new choices with capacity %zu", capacity);
    return choices;
}

bool choices_append(choices_t* choices, iso_data_t* iso_data) {
    if (!choices || !iso_data) {
        syslog(LOG_DEBUG, "Invalid input to choices_append");
        return false;
    }

    if(choices->len >= choices->cap) {
        syslog(LOG_DEBUG, "Choices list full (len=%d, cap=%d)", choices->len, choices->cap);
        return false;
    }

    // Store original values for debug
    syslog(LOG_DEBUG, "Appending choice: label=%s, content_id=%s", 
           iso_data->label ? iso_data->label : "NULL",
           iso_data->content_id ? iso_data->content_id : "NULL");

    // Use the passed iso_data directly instead of copying
    choices->values[choices->len++] = iso_data;
    syslog(LOG_DEBUG, "Successfully appended choice at index %d", choices->len - 1);
    return true;
}

void choices_free(choices_t* choices) {
    if (!choices) {
        return;
    }

    syslog(LOG_DEBUG, "Freeing choices structure (len=%d)", choices->len);
    
    if (choices->values) {
        for (int i = 0; i < choices->len; i++) {
            if (choices->values[i]) {
                free(choices->values[i]->label);
                free(choices->values[i]->url);
                free(choices->values[i]->content_id);
                free(choices->values[i]->sha256sum);
                free(choices->values[i]);
            }
        }
        free(choices->values);
    }
    
    free(choices);
    syslog(LOG_DEBUG, "Choices structure freed");
}