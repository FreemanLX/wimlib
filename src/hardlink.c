#include "wimlib_internal.h"
#include "dentry.h"
#include "list.h"
#include "lookup_table.h"

struct link_group {
	u64 link_group_id;
	struct link_group *next;
	struct list_head *dentry_list;
};

struct link_group_table {
	struct link_group **array;
	u64 num_entries;
	u64 capacity;
};

#include <sys/mman.h>

struct link_group_table *new_link_group_table(u64 capacity)
{
	return (struct link_group_table*)new_lookup_table(capacity);
}

/* Insert a dentry into the hard link group table based on its hard link group
 * ID.
 *
 * If there is already a dentry in the table having the same hard link group ID,
 * we link the dentries together in a circular list.
 *
 * If the hard link group ID is 0, this is a no-op and the dentry is not
 * inserted.
 */
int link_group_table_insert(struct dentry *dentry, struct link_group_table *table)
{
	size_t pos;
	struct link_group *group;

	if (dentry->hard_link == 0) {
		INIT_LIST_HEAD(&dentry->link_group_list);
		return 0;
	}

	/* Try adding to existing hard link group */
	pos = dentry->hard_link % table->capacity;
	group = table->array[pos];
	while (group) {
		if (group->link_group_id == dentry->hard_link) {
			list_add(&dentry->link_group_list, group->dentry_list);
			return 0;
		}
		group = group->next;
	}

	/* Add new hard link group to the table */

	group = MALLOC(sizeof(struct link_group));
	if (!group)
		return WIMLIB_ERR_NOMEM;
	group->link_group_id   = dentry->hard_link;
	group->next            = table->array[pos];
	INIT_LIST_HEAD(&dentry->link_group_list);
	group->dentry_list = &dentry->link_group_list;
	table->array[pos]      = group;

	/* XXX Make the table grow when too many entries have been inserted. */
	table->num_entries++;
	return 0;
}

/* Frees a link group table. */
void free_link_group_table(struct link_group_table *table)
{
	if (!table)
		return;
	if (table->array) {
		for (u64 i = 0; i < table->capacity; i++) {
			struct link_group *group = table->array[i];
			struct link_group *next;
			while (group) {
				next = group->next;
				FREE(group);
				group = next;
			}
		}
		FREE(table->array);
	}
	FREE(table);
}

/* Assign the link group IDs to dentries in a link group table, and return the
 * next available link group ID. */
u64 assign_link_groups(struct link_group_table *table)
{
	struct link_group *remaining_groups = NULL;
	u64 id = 1;
	for (u64 i = 0; i < table->capacity; i++) {
		struct link_group *group = table->array[i];
		struct link_group *next_group;
		struct dentry *dentry;
		while (group) {
			next_group = group->next;
			u64 cur_id;
			struct list_head *dentry_list = group->dentry_list;
			if (dentry_list->next == dentry_list) {
				/* Hard link group of size 1.  Change the hard
				 * link ID to 0 and discard the link_group */
				cur_id = 0;
				FREE(group);
			} else {
				/* Hard link group of size > 1.  Assign the
				 * dentries in the group the next available hard
				 * link IDs and queue the group to be
				 * re-inserted into the table. */
				cur_id = id++;
				group->next = remaining_groups;
				remaining_groups = group;
			}
			struct list_head *cur = dentry_list;
			do {
				dentry = container_of(cur,
						      struct dentry,
						      link_group_list);
				dentry->hard_link = cur_id;
				cur = cur->next;
			} while (cur != dentry_list);
			group = next_group;
		}
	}
	memset(table->array, 0, table->capacity * sizeof(table->array[0]));
	table->num_entries = 0;
	while (remaining_groups) {
		struct link_group *group = remaining_groups;
		size_t pos = group->link_group_id % table->capacity;

		table->num_entries++;
		group->next = table->array[pos];
		table->array[pos] = group;
		remaining_groups = remaining_groups->next;
	}
	return id;
}

static int link_group_free_duplicate_data(struct link_group *group)
{
	struct list_head *head;
	struct dentry *master;

	head = group->dentry_list;
	master = container_of(head, struct dentry, link_group_list);
	head = head->next;
	master->link_group_master_status = GROUP_MASTER;
	while (head != group->dentry_list) {
		int ret = share_dentry_ads(master,
					   container_of(head, struct dentry,
						        link_group_list));
		if (ret != 0)
			return ret;
	}
	return 0;
}

int link_groups_free_duplicate_data(struct link_group_table *table)
{
	for (u64 i = 0; i < table->capacity; i++) {
		struct link_group *group = table->array[i];
		while (group) {
			int ret = link_group_free_duplicate_data(group);
			if (ret != 0)
				return ret;
			group = group->next;
		}
	}
	return 0;
}
